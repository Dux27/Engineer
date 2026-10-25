import json
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.metrics import (
    balanced_accuracy_score,
    classification_report,
    confusion_matrix,
    f1_score,
    precision_recall_fscore_support,
)
from sklearn.preprocessing import LabelEncoder
from sklearn.utils.class_weight import compute_sample_weight
from xgboost import XGBClassifier

TIME_WINDOW = 12.0
CAL = True

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR.parent / "datasets"
SUFFIX = f"{int(TIME_WINDOW)}s{'_cal' if CAL else ''}"
TRAIN_DIR = DATA_DIR / f"train_{SUFFIX}"
VAL_DIR = DATA_DIR / f"val_{SUFFIX}"
TEST_DIR = DATA_DIR / f"test_{SUFFIX}"
OUT_DIR = BASE_DIR.parent / "models"

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


def load_Xy(directory: Path):
    X = pd.read_parquet(directory / "X.parquet").drop(columns="subject", errors="ignore")
    y = pd.read_parquet(directory / "y.parquet")["label"]
    return X, y


def predictWithTreshold(model, X, threshold_dict, le):
    proba = model.predict_proba(X)
    pred = np.argmax(proba, axis=1)
    for class_name, threshold in threshold_dict.items():
        class_idx = list(le.classes_).index(class_name)
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
        results.append({
            "threshold": th,
            "precision": precision[0],
            "recall": recall[0],
            "f1": f1[0],
            "macro_f1": f1_score(y_val_enc, pred, average="macro"),
        })
    results_df = pd.DataFrame(results)
    best_idx = results_df["f1"].idxmax()
    return float(results_df.loc[best_idx, "threshold"]), float(results_df.loc[best_idx, "f1"]), results_df


def report_metrics(name, y_true, y_pred, le):
    macro_f1 = f1_score(y_true, y_pred, average="macro")
    bal_acc = balanced_accuracy_score(y_true, y_pred)
    print(f"\n{name}")
    print(classification_report(y_true, y_pred, labels=np.arange(len(le.classes_)), target_names=le.classes_, zero_division=0))
    print(confusion_matrix(y_true, y_pred, labels=np.arange(len(le.classes_))))
    print(f"Macro-F1: {macro_f1:.4f}")
    print(f"Balanced accuracy: {bal_acc:.4f}")
    return {"macro_f1": float(macro_f1), "balanced_accuracy": float(bal_acc)}


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    X_train, y_train = load_Xy(TRAIN_DIR)
    X_val, y_val = load_Xy(VAL_DIR)
    le = LabelEncoder()
    y_train_enc = le.fit_transform(y_train.astype(str))
    y_val_enc = le.transform(y_val.astype(str))
    X_val = X_val[X_train.columns]
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
    model.fit(X_train, y_train_enc, eval_set=[(X_val, y_val_enc)], verbose=20, sample_weight=sample_weight)

    baseline_metrics = report_metrics("Validation: default prediction", y_val_enc, model.predict(X_val), le)
    best_th, best_f1, tuning_results = tuneThresholdForClass(
        model, X_val, y_val_enc, "amusement", le, thresholds=np.arange(0.01, 0.99, 0.01)
    )
    print(f"\nBest amusement threshold: {best_th:.2f} (F1={best_f1:.4f})")
    threshold_dict = {"amusement": best_th}
    tuned_metrics = report_metrics(
        "Validation: tuned amusement threshold",
        y_val_enc,
        predictWithTreshold(model, X_val, threshold_dict, le),
        le,
    )
    print(f"\nMacro-F1 improvement: {tuned_metrics['macro_f1'] - baseline_metrics['macro_f1']:+.4f}")
    print(f"Balanced accuracy improvement: {tuned_metrics['balanced_accuracy'] - baseline_metrics['balanced_accuracy']:+.4f}")

    # Final evaluation; do not use test results to adjust the model or threshold.
    X_test, y_test = load_Xy(TEST_DIR)
    X_test = X_test[X_train.columns]
    y_test_enc = le.transform(y_test.astype(str))
    test_metrics = report_metrics(
        "Final test: tuned amusement threshold",
        y_test_enc,
        predictWithTreshold(model, X_test, threshold_dict, le),
        le,
    )

    model_path = OUT_DIR / "xgb_model.json"
    model.save_model(model_path)
    meta = {
        "label_classes": le.classes_.tolist(),
        "feature_columns": X_train.columns.tolist(),
        "best_iteration": int(model.best_iteration),
        "best_score": float(model.best_score),
        "baseline_metrics": baseline_metrics,
        "tuned_metrics": {**tuned_metrics, "thresholds": threshold_dict},
        "test_metrics": test_metrics,
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
        },
    }
    (OUT_DIR / "metadata.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    tuning_results.to_csv(OUT_DIR / "threshold_tuning.csv", index=False)
    print(f"\nModel saved to: {model_path}")
    print(f"Metadata saved to: {OUT_DIR / 'metadata.json'}")
    print(f"Threshold tuning results saved to: {OUT_DIR / 'threshold_tuning.csv'}")


if __name__ == "__main__":
    main()
