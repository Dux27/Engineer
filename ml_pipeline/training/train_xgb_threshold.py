import os
import json
import numpy as np
import pandas as pd

from sklearn.preprocessing import LabelEncoder
from sklearn.utils.class_weight import compute_sample_weight
from sklearn.metrics import classification_report, confusion_matrix, f1_score, balanced_accuracy_score, precision_recall_fscore_support

from xgboost import XGBClassifier

TIME_WINDOW = 15.0  
CAL = True

TRAIN_DIR = f"WESAD/model/train_{int(TIME_WINDOW)}s{'_cal' if CAL else ''}"
VAL_DIR   = f"WESAD/model/val_{int(TIME_WINDOW)}s{'_cal' if CAL else ''}"
OUT_DIR   = "WESAD/model/out"

N_ESTIMATORS = 500
LEARNING_RATE = 0.03
MAX_DEPTH = 6
SUBSAMPLE = 0.8
COLSAMPLE_BYTREE = 0.8
REG_LAMBDA = 2.0
REG_ALPHA = 0.5
GAMMA = 0.1
MIN_CHILD_WEIGHT = 3.7
TREE_METHOD = "hist"  
EARLY_STOPPING_ROUNDS = 50
RANDOM_STATE = 29


def load_Xy(dir_path: str, label_col: str):
    X = pd.read_parquet(os.path.join(dir_path, "X.parquet"))
    y_df = pd.read_parquet(os.path.join(dir_path, "y.parquet"))
    y = y_df[label_col]
    
    if 'subject' in X.columns:
        X = X.drop(columns=['subject'])

    return X, y


def predictWithTreshold(model, X, threshold_dict, le):
    proba = model.predict_proba(X)
    pred = np.argmax(proba, axis=1)  # Default: highest probability
    
    # Apply custom thresholds
    for class_name, threshold in threshold_dict.items():
        class_idx = list(le.classes_).index(class_name)
        # Override prediction if probability > threshold
        pred = np.where(proba[:, class_idx] > threshold, class_idx, pred)
    
    return pred


def tuneThresholdForClass(model, X_val, y_val_enc, class_name, le, thresholds=np.arange(0.10, 0.50, 0.05)):
    class_idx = list(le.classes_).index(class_name)
    proba = model.predict_proba(X_val)
    
    results = []
    
    for th in thresholds:
        pred = np.argmax(proba, axis=1)
        pred = np.where(proba[:, class_idx] > th, class_idx, pred)

        precision, recall, f1, _ = precision_recall_fscore_support(
            y_val_enc, pred, labels=[class_idx], average=None, zero_division=0
        )
        
        macro_f1 = f1_score(y_val_enc, pred, average="macro")
        
        results.append({
            "threshold": th,
            "precision": precision[0],
            "recall": recall[0],
            "f1": f1[0],
            "macro_f1": macro_f1
        })
    
    results_df = pd.DataFrame(results)
    
    # Find best threshold (maximize class F1)
    best_idx = results_df["f1"].idxmax()
    best_threshold = results_df.loc[best_idx, "threshold"]
    best_f1 = results_df.loc[best_idx, "f1"]
    
    return best_threshold, best_f1, results_df


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    X_train, y_train = load_Xy(TRAIN_DIR, "label")
    X_val, y_val = load_Xy(VAL_DIR, "label")

    le = LabelEncoder()
    y_train_enc = le.fit_transform(y_train.astype(str))
    y_val_enc = le.transform(y_val.astype(str))

    sample_weight = compute_sample_weight(class_weight="balanced", y=y_train_enc)

    print(f"Classes: {le.classes_}")
    print(f"Training samples: {len(X_train)}")
    print(f"Validation samples: {len(X_val)}")
    print(f"Features: {len(X_train.columns)}\n")

    model = XGBClassifier(
        objective="multi:softprob",
        num_class=len(le.classes_),
        n_estimators=N_ESTIMATORS,
        learning_rate=LEARNING_RATE,
        max_depth=MAX_DEPTH,
        subsample=SUBSAMPLE,
        colsample_bytree=COLSAMPLE_BYTREE,
        reg_lambda=REG_LAMBDA,
        reg_alpha=REG_ALPHA,
        gamma=GAMMA,
        min_child_weight=MIN_CHILD_WEIGHT,
        tree_method=TREE_METHOD,
        eval_metric="mlogloss",
        random_state=RANDOM_STATE,
        n_jobs=-1,
        early_stopping_rounds=EARLY_STOPPING_ROUNDS,
    )

    model.fit(
        X_train,
        y_train_enc,
        eval_set=[(X_val, y_val_enc)],
        verbose=20,
        sample_weight=sample_weight,
    )

    print("\n Baseline Prediction (default threshold)")
    
    val_pred_baseline = model.predict(X_val)
    macro_f1_baseline = f1_score(y_val_enc, val_pred_baseline, average="macro")
    bal_acc_baseline = balanced_accuracy_score(y_val_enc, val_pred_baseline)

    print("\nValidation classification report:")
    print(classification_report(y_val_enc, val_pred_baseline, target_names=le.classes_))
    print("\nConfusion matrix:")
    print(confusion_matrix(y_val_enc, val_pred_baseline))
    print(f"\nMacro-F1:          {macro_f1_baseline:.4f}")
    print(f"Balanced accuracy: {bal_acc_baseline:.4f}")

    print("\n Tuning threshold for 'amusement'")
    
    best_th, best_f1, tuning_results = tuneThresholdForClass(
        model, X_val, y_val_enc, "amusement", le,
        thresholds=np.arange(0.01, 0.99, 0.01)  
    )
    
    print("Threshold tuning results:")
    print(tuning_results.to_string(index=False))
    print(f"\n Best threshold for 'amusement': {best_th:.2f} (F1={best_f1:.4f})")

    print(f" Prediction with optimized threshold (amusement > {best_th:.2f})")
    
    threshold_dict = {"amusement": best_th}
    val_pred_tuned = predictWithTreshold(model, X_val, threshold_dict, le)
    
    macro_f1_tuned = f1_score(y_val_enc, val_pred_tuned, average="macro")
    bal_acc_tuned = balanced_accuracy_score(y_val_enc, val_pred_tuned)

    print("\nValidation classification report:")
    print(classification_report(y_val_enc, val_pred_tuned, target_names=le.classes_))
    print("\nConfusion matrix:")
    print(confusion_matrix(y_val_enc, val_pred_tuned))
    print(f"\nMacro-F1:          {macro_f1_tuned:.4f}")
    print(f"Balanced accuracy: {bal_acc_tuned:.4f}")

    print("\n IMPROVEMENT SUMMARY")
    print(f"Baseline Macro-F1:       {macro_f1_baseline:.4f}")
    print(f"Tuned Macro-F1:          {macro_f1_tuned:.4f}")
    print(f"Improvement:             {macro_f1_tuned - macro_f1_baseline:+.4f}")
    print(f"\nBaseline Balanced Acc:   {bal_acc_baseline:.4f}")
    print(f"Tuned Balanced Acc:      {bal_acc_tuned:.4f}")
    print(f"Improvement:             {bal_acc_tuned - bal_acc_baseline:+.4f}")

    model_path = os.path.join(OUT_DIR, "xgb_model.json")
    model.save_model(model_path)

    meta = {
        "label_classes": le.classes_.tolist(),
        "feature_columns": X_train.columns.tolist(),
        "best_iteration": int(model.best_iteration),
        "best_score": float(model.best_score),
        "baseline_metrics": {
            "macro_f1": float(macro_f1_baseline),
            "balanced_accuracy": float(bal_acc_baseline),
        },
        "tuned_metrics": {
            "macro_f1": float(macro_f1_tuned),
            "balanced_accuracy": float(bal_acc_tuned),
            "thresholds": threshold_dict,
        },
        "params": {
            "n_estimators": N_ESTIMATORS,
            "learning_rate": LEARNING_RATE,
            "max_depth": MAX_DEPTH,
            "subsample": SUBSAMPLE,
            "colsample_bytree": COLSAMPLE_BYTREE,
            "reg_lambda": REG_LAMBDA,
            "reg_alpha": REG_ALPHA,
            "gamma": GAMMA,
            "min_child_weight": MIN_CHILD_WEIGHT,
            "early_stopping_rounds": EARLY_STOPPING_ROUNDS,
            "tree_method": TREE_METHOD,
        }
    }

    with open(os.path.join(OUT_DIR, "metadata.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)

    tuning_results.to_csv(os.path.join(OUT_DIR, "threshold_tuning.csv"), index=False)

    print(f"\nModel saved to: {model_path}")
    print(f"Metadata saved to: {os.path.join(OUT_DIR, 'metadata.json')}")
    print(f"Threshold tuning results saved to: {os.path.join(OUT_DIR, 'threshold_tuning.csv')}")


if __name__ == "__main__":
    main()
