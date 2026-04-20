import os
import json
import numpy as np
import pandas as pd

from sklearn.preprocessing import LabelEncoder
from sklearn.utils.class_weight import compute_sample_weight
from sklearn.metrics import classification_report, confusion_matrix, f1_score, balanced_accuracy_score

from xgboost import XGBClassifier
from lightgbm import LGBMClassifier
from sklearn.ensemble import RandomForestClassifier, ExtraTreesClassifier


TRAIN_DIR = "WESAD/model/train_10s_cal"
VAL_DIR   = "WESAD/model/val_10s_cal"
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


THRESHOLDS = {
    "amusement": 0.37
}


def load_Xy(dir_path: str, label_col: str):
    X = pd.read_parquet(os.path.join(dir_path, "X.parquet"))
    y_df = pd.read_parquet(os.path.join(dir_path, "y.parquet"))
    y = y_df[label_col]
    
    # Remove subject column
    if 'subject' in X.columns:
        X = X.drop(columns=['subject'])

    return X, y


def predict_with_threshold(model, X, threshold_dict, le):
    proba = model.predict_proba(X)
    pred = np.argmax(proba, axis=1) 
    
    for class_name, threshold in threshold_dict.items():
        class_idx = list(le.classes_).index(class_name)
        pred = np.where(proba[:, class_idx] > threshold, class_idx, pred)
    
    return pred


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
        verbose=10,
        sample_weight=sample_weight,
    )

    val_pred = predict_with_threshold(model, X_val, THRESHOLDS, le)

    macro_f1 = f1_score(y_val_enc, val_pred, average="macro")
    bal_acc = balanced_accuracy_score(y_val_enc, val_pred)

    print("Validation classification report:")
    print(classification_report(y_val_enc, val_pred, target_names=le.classes_))

    print("\nValidation confusion matrix:")
    print(confusion_matrix(y_val_enc, val_pred))

    print(f"\nExtra metrics:")
    print(f"  Macro-F1:          {macro_f1:.4f}")
    print(f"  Balanced accuracy: {bal_acc:.4f}")

    model_path = os.path.join(OUT_DIR, "xgb_model.json")
    model.save_model(model_path)

    meta = {
        "label_classes": le.classes_.tolist(),
        "feature_columns": X_train.columns.tolist(),
        "best_iteration": int(getattr(model, "best_iteration", -1)),
        "best_score": float(getattr(model, "best_score", np.nan)),
        "macro_f1": float(macro_f1),
        "balanced_accuracy": float(bal_acc),
        "thresholds": THRESHOLDS,  
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

    print(f"\nModel saved to: {model_path}")
    print(f"Metadata saved to: {os.path.join(OUT_DIR, 'metadata.json')}")


if __name__ == "__main__":
    main()
