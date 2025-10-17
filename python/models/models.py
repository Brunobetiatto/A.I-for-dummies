# python/models.py
from __future__ import annotations
from typing import Tuple, List, Optional, Dict, Any, Union
from pathlib import Path
import os, sys, json, time, argparse

if os.name == "nt":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim

import math

try:
    import pandas as pd
    from pandas import DataFrame, Series
except Exception:
    DataFrame = Any  # type: ignore
    Series = Any     # type: ignore
    pd = None        # type: ignore

# --- scikit-learn (used for classical models + preprocessing + some metrics) ---
_SK_OK = True
try:
    # preprocessing
    from sklearn.preprocessing import StandardScaler, MinMaxScaler, OneHotEncoder
    from sklearn.compose import ColumnTransformer
    from sklearn.pipeline import Pipeline
    from sklearn.impute import SimpleImputer
    # models
    from sklearn.tree import DecisionTreeClassifier, DecisionTreeRegressor
    from sklearn.ensemble import RandomForestClassifier, RandomForestRegressor
    from sklearn.neighbors import KNeighborsClassifier, KNeighborsRegressor
    from sklearn.naive_bayes import GaussianNB
    from sklearn.svm import SVC, SVR
    from sklearn.ensemble import GradientBoostingClassifier, GradientBoostingRegressor
    from sklearn.multiclass import OneVsRestClassifier
    # metrics
    from sklearn.metrics import f1_score
except Exception:
    _SK_OK = False

# headless plotting
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap

CACHE_PATH = Path("./cache")
Tensorable = Union[np.ndarray, List[float], List[int], "DataFrame", "Series"]


class MLPReg(torch.nn.Module):
    def __init__(self, in_dim, hidden=64):
        super().__init__()
        self.net = torch.nn.Sequential(
            torch.nn.Linear(in_dim, hidden),
            torch.nn.ReLU(),
            torch.nn.Linear(hidden, hidden),
            torch.nn.ReLU(),
            torch.nn.Linear(hidden, 1),
        )
    def forward(self, x): return self.net(x).squeeze(-1)

class MLPCls(torch.nn.Module):
    def __init__(self, in_dim, n_classes, hidden=64):
        super().__init__()
        self.net = torch.nn.Sequential(
            torch.nn.Linear(in_dim, hidden),
            torch.nn.ReLU(),
            torch.nn.Linear(hidden, hidden),
            torch.nn.ReLU(),
            torch.nn.Linear(hidden, n_classes),
        )
    def forward(self, x): return self.net(x)

# ---------- small helpers (kept from your file) ----------
def _ensure_cache_dir(path: Path = CACHE_PATH) -> Path:
    path.mkdir(parents=True, exist_ok=True); return path

def _to_numpy(x: Tensorable) -> np.ndarray:
    if pd is not None and isinstance(x, (pd.DataFrame, pd.Series)):
        return x.to_numpy()
    return np.asarray(x)

def _infer_task(y: np.ndarray, task: Optional[str] = None) -> str:
    if task in {"classification", "regression"}:
        return task
    if np.issubdtype(y.dtype, np.floating):  return "regression"
    if np.issubdtype(y.dtype, np.integer):   return "classification"
    uniques = np.unique(y)
    return "classification" if len(uniques) <= max(20, int(np.sqrt(y.shape[0]) + 0.5)) else "regression"

def _r2_score(y_true: np.ndarray, y_pred: np.ndarray) -> float:
    y_true = y_true.astype(np.float64); y_pred = y_pred.astype(np.float64)
    ss_res = np.sum((y_true - y_pred) ** 2)
    ss_tot = np.sum((y_true - np.mean(y_true)) ** 2)
    return float(1.0 - ss_res / (ss_tot if ss_tot != 0 else 1.0))

def _accuracy(y_true: np.ndarray, y_pred_labels: np.ndarray) -> float:
    y_true = y_true.reshape(-1); y_pred_labels = y_pred_labels.reshape(-1)
    return float((y_true == y_pred_labels).mean()) if y_true.size else 0.0

def _device_from_fit_params(fp: Optional[Dict[str, Any]]) -> torch.device:
    dev = (fp or {}).get("device", None)
    if isinstance(dev, torch.device): return dev
    if isinstance(dev, str):          return torch.device(dev)
    return torch.device("cuda" if torch.cuda.is_available() else "cpu")

def _make_model(input_dim: int, task: str, num_classes: Optional[int]) -> nn.Module:
    if task == "regression":
        return nn.Sequential(nn.Linear(input_dim, 64), nn.ReLU(), nn.Linear(64, 1))
    # classification
    C = int(num_classes or 2)
    return nn.Sequential(nn.Linear(input_dim, 64), nn.ReLU(), nn.Linear(64, C if C > 2 else 1))

def _encode_labels_for_plot(y):
    """
    Return (y_idx, classes, class_to_idx) for single-label y.
    Works when y is (N,), (N,1), numeric or string labels.
    """
    y = np.asarray(y)
    if y.ndim > 1 and y.shape[1] == 1:
        y = y[:, 0]
    # numeric → keep as-is (cast to int)
    if np.issubdtype(y.dtype, np.number):
        classes = np.unique(y)
        y_idx = y.astype(int).reshape(-1)
        class_to_idx = {int(c): int(i) for i, c in enumerate(classes)}
        return y_idx, classes, class_to_idx
    # strings / objects → make a mapping
    y_str = y.astype(str).reshape(-1)
    classes = np.unique(y_str)
    class_to_idx = {c: i for i, c in enumerate(classes)}
    y_idx = np.vectorize(class_to_idx.get, otypes=[np.int64])(y_str).astype(int).reshape(-1)
    return y_idx, classes, class_to_idx

def _train(
    dataX: Tensorable, dataY: Tensorable,
    testX: Tensorable, testY: Tensorable,
    trainee: nn.Module,
    fit_params: Optional[Dict[str, Any]] = None,
    progress_cb: Optional[Any] = None,
    pause_file: Optional[Path] = None,
    **_: Any,
) -> Tuple[nn.Module, float]:
    fp = dict(fit_params or {})
    device = _device_from_fit_params(fp)

    Xtr = _to_numpy(dataX); Xte = _to_numpy(testX)
    ytr = _to_numpy(dataY); yte = _to_numpy(testY)
    if Xtr.ndim == 1: Xtr = Xtr.reshape(-1, 1)
    if Xte.ndim == 1: Xte = Xte.reshape(-1, 1)

    task = _infer_task(ytr, fp.get("task"))
    num_classes = None
    if task == "classification":
        ytr = ytr.astype(np.int64); yte = yte.astype(np.int64)
        classes = np.unique(ytr); num_classes = int(classes.size)
        if num_classes == 2 and set(classes.tolist()) != {0, 1}:
            mapping = {c: i for i, c in enumerate(sorted(classes))}
            ytr = np.vectorize(mapping.get)(ytr)
            yte = np.vectorize(mapping.get)(yte)
            num_classes = int(np.unique(ytr).size)

    Xtr_t = torch.from_numpy(Xtr).to(device=device, dtype=torch.float32)
    Xte_t = torch.from_numpy(Xte).to(device=device, dtype=torch.float32)
    if task == "regression":
        ytr_t = torch.from_numpy(ytr.astype(np.float32)).to(device=device).view(-1, 1)
        yte_np = yte.astype(np.float32)
    else:
        if (num_classes or 2) == 2:
            ytr_t = torch.from_numpy(ytr.astype(np.float32)).to(device=device).view(-1, 1)
        else:
            ytr_t = torch.from_numpy(ytr.astype(np.int64)).to(device=device)
        yte_np = yte.astype(np.int64)

    trainee = trainee.to(device)
    optimizer = fp.get("optimizer_cls", optim.Adam)(trainee.parameters(), **fp.get("optimizer_params", {"lr": 1e-3}))

    loss_fn = fp.get("loss_fn", None)
    if loss_fn is None:
        if task == "regression":
            loss_fn = nn.MSELoss()
        else:
            if (num_classes or 2) == 2:
                w = fp.get("class_weights", None)
                w = torch.as_tensor(np.asarray(w, dtype=np.float32), device=device) if w is not None else None
                loss_fn = nn.BCEWithLogitsLoss(pos_weight=w) if w is not None else nn.BCEWithLogitsLoss()
            else:
                w = fp.get("class_weights", None)
                w = torch.as_tensor(np.asarray(w, dtype=np.float32), device=device) if w is not None else None
                loss_fn = nn.CrossEntropyLoss(weight=w)
    elif isinstance(loss_fn, nn.Module):
        loss_fn = loss_fn.to(device)

    epochs = int(fp.get("epochs", 50))
    bs = int(fp.get("batch_size", min(256, max(1, Xtr_t.shape[0]))))
    loader = torch.utils.data.DataLoader(torch.utils.data.TensorDataset(Xtr_t, ytr_t),
                                         batch_size=bs, shuffle=bool(fp.get("shuffle", True)))

    # Training loop with callbacks and pause-file
    for e in range(1, epochs + 1):
        trainee.train()
        running = 0.0; batches = 0
        for xb, yb in loader:
            if pause_file is not None:
                while pause_file.exists(): time.sleep(0.2)
            optimizer.zero_grad(set_to_none=True)
            logits = trainee(xb)
            if task == "regression":
                if logits.ndim == 1: logits = logits.view(-1, 1)
                loss = loss_fn(logits, yb)
            else:
                if (num_classes or 2) == 2:
                    logits = logits.view(-1, 1)
                    loss = loss_fn(logits, yb)
                else:
                    loss = loss_fn(logits, yb)
            loss.backward(); optimizer.step()
            running += float(loss.item()); batches += 1

        # eval-once per epoch
        trainee.eval()
        with torch.no_grad():
            logits_te = trainee(Xte_t).detach().cpu().numpy()
        if task == "regression":
            pred = logits_te.squeeze()
            score = _r2_score(yte_np, pred)
        else:
            if (num_classes or 2) == 2:
                if logits_te.ndim > 1 and logits_te.shape[1] == 1: logits_te = logits_te[:, 0]
                y_pred = (logits_te >= 0.0).astype(np.int64)
            else:
                y_pred = logits_te.argmax(axis=1).astype(np.int64) if logits_te.ndim > 1 else np.zeros_like(yte_np)
            score = _accuracy(yte_np, y_pred)

        avg_loss = running / max(1, batches)
        if progress_cb:
            progress_cb(epoch=e, epochs=epochs, loss=avg_loss, score=score)

    return trainee, float(score)

def _cache_model(model: nn.Module, model_name: Optional[str] = None,
                 cache_path: Path = CACHE_PATH, extra_meta: Optional[Dict[str, Any]] = None) -> Path:
    _ensure_cache_dir(cache_path)
    if model_name is None: model_name = model.__class__.__name__
    out_bin = cache_path / f"model.{model_name}.cache.pt"
    meta = {"model_name": model_name}; meta.update(extra_meta or {})
    # try saving state_dict; if it's not a torch Module, just write meta json
    try:
        torch.save({"state_dict": getattr(model, "state_dict", lambda: {})(), "meta": meta}, out_bin)
    except Exception:
        out_bin = cache_path / f"model.{model_name}.meta.json"
    try:
        (cache_path / f"model.{model_name}.meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    except Exception:
        pass
    return out_bin

def _train_and_cache(
    dataX: Tensorable, dataY: Tensorable, testX: Tensorable, testY: Tensorable,
    trainee: Any, model_name: Optional[str] = None, cache_path: Path = CACHE_PATH,
    fit_params: Optional[Dict[str, Any]] = None, pause_file: Optional[Path] = None, **kwargs: Any,
) -> Tuple[Any, float, Path]:
    fp = fit_params or {}
    task = fp.get("task", None)
    Xnp = _to_numpy(dataX);  Xnp = Xnp.reshape(-1, 1) if Xnp.ndim == 1 else Xnp

    def emit(**payload): print(json.dumps(payload), flush=True)

    emit(event="begin", task=task, input_dim=int(Xnp.shape[1]), params=fp)

    if isinstance(trainee, nn.Module):
        model, score = _train(
            dataX, dataY, testX, testY, trainee,
            fit_params=fp, pause_file=pause_file,
            progress_cb=lambda **p: emit(event="epoch", **p)
        )
    else:
        # sklearn-like: fit once, compute a single 'epoch' event for consistency
        Xtr = _to_numpy(dataX); ytr = _to_numpy(dataY)
        Xte = _to_numpy(testX); yte = _to_numpy(testY)
        model = trainee.fit(Xtr, ytr)
        # derive a simple score for the progress line
        if fp.get("task") == "classification" and yte.ndim == 1:
            yhat = model.predict(Xte)
            score = float(_accuracy(yte, yhat))
        elif fp.get("task") == "multilabel" and yte.ndim == 2:
            from sklearn.metrics import f1_score
            yhat = np.asarray(model.predict(Xte))
            score = float(f1_score(yte, yhat, average="micro", zero_division=0))
        else:
            yhat = np.asarray(model.predict(Xte)).reshape(-1)
            score = float(_r2_score(yte.reshape(-1), yhat))
        emit(event="epoch", epoch=fp.get("epochs", 1), epochs=fp.get("epochs", 1), loss=0.0, score=score)

    saved = _cache_model(model, model_name=model_name, cache_path=cache_path, extra_meta={
        "task": task,
        "input_dim": int(Xnp.shape[1]),
        "score_name": "accuracy" if (task or _infer_task(_to_numpy(dataY))) == "classification" else "r2",
        "score_value": float(score),
    })
    emit(event="done", score=float(score), path=str(saved))
    return model, score, saved

def is_binary_series(s: pd.Series) -> bool:
    if s.dtype == bool:
        return True
    if pd.api.types.is_integer_dtype(s):
        uniq = np.unique(s.dropna().values)
        return len(uniq) == 2 and set(uniq).issubset({0,1})
    # allow float 0/1 too
    if pd.api.types.is_float_dtype(s):
        uniq = np.unique(np.round(s.dropna().values))
        return len(uniq) == 2 and set(uniq).issubset({0,1})
    return False

def train_test_split(X, y, train_pct: float, seed: int = 123):
    n = X.shape[0]
    idx = np.arange(n)
    rng = np.random.default_rng(seed)
    rng.shuffle(idx)
    k = max(1, min(n-1, int(round(train_pct * n))))
    train_idx, test_idx = idx[:k], idx[k:]
    return X[train_idx], X[test_idx], y[train_idx], y[test_idx]

def ascii_confusion(y_true, y_pred):
    # expects 0/1
    y_true = y_true.astype(int)
    y_pred = y_pred.astype(int)
    tp = int(((y_true==1) & (y_pred==1)).sum())
    tn = int(((y_true==0) & (y_pred==0)).sum())
    fp = int(((y_true==0) & (y_pred==1)).sum())
    fn = int(((y_true==1) & (y_pred==0)).sum())
    lines = []
    lines.append("Confusion Matrix (rows=true, cols=pred)")
    lines.append("")
    lines.append("           Pred 0   Pred 1")
    lines.append(f"True 0   |  {tn:6d}   {fp:6d}")
    lines.append(f"True 1   |  {fn:6d}   {tp:6d}")
    return "\n".join(lines), tp, tn, fp, fn

def classification_metrics(tp, tn, fp, fn):
    acc = (tp+tn)/max(1,(tp+tn+fp+fn))
    prec = tp/max(1,(tp+fp))
    rec  = tp/max(1,(tp+fn))
    f1   = 2*prec*rec/max(1e-12,(prec+rec))
    return acc, prec, rec, f1

def regression_metrics(y_true, y_pred):
    y_true = y_true.astype(float)
    y_pred = y_pred.astype(float)
    mae = np.mean(np.abs(y_true - y_pred))
    mse = np.mean((y_true - y_pred)**2)
    rmse = math.sqrt(mse)
    # R^2
    ss_res = np.sum((y_true - y_pred)**2)
    ss_tot = np.sum((y_true - np.mean(y_true))**2)
    r2 = 1.0 - (ss_res / max(1e-12, ss_tot))
    return r2, mae, mse, rmse

# -------- 2D projection (kept) ----------
def project_2d(X, method="pca2"):
    X = np.asarray(X)
    if X.ndim == 1:
        return X.reshape(-1,1), np.zeros_like(X)
    if method == "pca2":
        from sklearn.decomposition import PCA
        Z = PCA(n_components=2).fit_transform(X)
        return Z[:,0], Z[:,1]
    elif method == "tsne2":
        from sklearn.manifold import TSNE
        Z = TSNE(n_components=2, init="pca", learning_rate="auto").fit_transform(X)
        return Z[:,0], Z[:,1]
    else:
        # none -> take first two features (pad if needed)
        if X.shape[1] == 1:
            return X[:,0], np.zeros(len(X))
        a = X[:,0]; b = X[:,1] if X.shape[1] > 1 else np.zeros(len(X))
        return a, b

# -------- unified predict/proba helpers (so plots work with torch or sklearn) ---
def _predict_scores_or_probs(model: Any, X: np.ndarray) -> Tuple[np.ndarray, int, str]:
    """
    Returns (S, C_eff, mode), where:
      - for binary cls: S is 1D prob for class=1 in [0,1], C_eff=2, mode="prob"
      - for multi-class: S is (N,C) prob per class, C_eff=C, mode="probs"
      - for regression:  S is 1D prediction, C_eff=1, mode="reg"
    Tries sklearn API first; falls back to torch.
    """
    # sklearn branch
    if hasattr(model, "predict"):
        try:
            if hasattr(model, "predict_proba"):
                P = model.predict_proba(X)
                if isinstance(P, list):  # multilabel OneVsRest can return list
                    P = np.column_stack([p[:,1] if p.shape[1]>1 else p.reshape(-1) for p in P])
                if P.ndim == 2 and P.shape[1] >= 2:
                    return P, P.shape[1], "probs"
                elif P.ndim == 1:
                    return P, 2, "prob"
            if hasattr(model, "decision_function"):
                df = model.decision_function(X)
                if df.ndim == 1:
                    P = 1.0/(1.0+np.exp(-df))
                    return P, 2, "prob"
                else:
                    # softmax decision to probs
                    e = np.exp(df - df.max(1, keepdims=True))
                    P = e / e.sum(1, keepdims=True)
                    return P, P.shape[1], "probs"
            # fallback to plain predict
            y = model.predict(X)
            if y.ndim == 1:
                return y, 1, "reg"
            return y, y.shape[1], "reg"
        except Exception:
            pass

    # torch branch
    with torch.no_grad():
        t = torch.from_numpy(X).float()
        logits = model(t)
        if logits.dim() == 1:
            # could be binary cls (logit) or regression
            # assume binary prob if values are not huge; we still pass as prob
            p = torch.sigmoid(logits.view(-1)).cpu().numpy()
            return p, 2, "prob"
        if logits.shape[1] == 1:
            p = torch.sigmoid(logits[:,0]).cpu().numpy()
            return p, 2, "prob"
        if logits.shape[1] > 1:
            probs = torch.softmax(logits, dim=1).cpu().numpy()
            return probs, probs.shape[1], "probs"
    # regression final fallback
    return logits.squeeze().cpu().numpy(), 1, "reg"  # type: ignore

# ---------------- plots (tweaked to accept sklearn models) --------------------
def save_plot_regression(X, y, model, epoch, n_epochs, out_path,
                         x_label=None, y_label=None, proj="pca2", color_by="residual"):
    """
    Live regression plot.
    * 1-D: scatter of (x, y) with a RED fitted curve (sorted by x).
    * N-D: 2-D projection (PCA/t-SNE/none) colored by residuals.
    Writes atomically to out_path so the GTK side can reload safely.
    """
    X = np.asarray(X); y = np.asarray(y).reshape(-1)
    try:
        if hasattr(model, "predict"):
            yp = np.asarray(model.predict(X)).reshape(-1)
        else:
            with torch.no_grad():
                yp = model(torch.from_numpy(X).float()).cpu().numpy().reshape(-1)
    except Exception:
        yp = np.zeros_like(y)

    res = y - yp

    fig, ax = plt.subplots(figsize=(6.5, 4.2), dpi=110)

    if X.shape[1] == 1:
        xs = X[:, 0]
        ax.scatter(xs, y, s=12, alpha=0.70)
        order = np.argsort(xs)
        ax.plot(xs[order], yp[order], color="red", linewidth=2.0)  # <- red fit line
        ax.set_xlabel(x_label or "X")
        ax.set_ylabel(y_label or "y")
    else:
        X0, X1 = project_2d(X, method=proj)
        cb = ax.scatter(X0, X1, c=res, s=16, cmap="viridis", alpha=0.85)
        fig.colorbar(cb, ax=ax, label="residual")
        ax.set_xlabel("PC1" if proj != "none" else (x_label or "X"))
        ax.set_ylabel("PC2" if proj != "none" else (y_label or "y"))

    ax.set_title(f"Fitting — epoch {epoch}/{n_epochs}")
    ax.grid(True, alpha=0.25)
    plt.tight_layout()

    tmp = out_path + ".tmp.png"
    plt.savefig(tmp, dpi=110, bbox_inches="tight")
    plt.close(fig)
    os.replace(tmp, out_path)

def save_plot_classification(X, y_idx_or_multi, model, epoch, epochs, out_path,
                             feature_names=None, device="cpu", proj="pca2"):
    """
    1D: points + probability curve; 2D: decision surface; High-D: 2-panel PCA/TSNE.
    Now robust to string labels (encodes them to numeric for coloring).
    """
    X = np.asarray(X, dtype=np.float32)
    y_arr = np.asarray(y_idx_or_multi)
    d = X.shape[1]

    plt.clf()
    plt.figure(figsize=(10, 4.6), dpi=110)
    plt.suptitle(f"Training epoch {epoch}/{epochs}")

    # Multilabel?
    is_multilabel = (y_arr.ndim == 2 and set(np.unique(y_arr)) <= {0, 1})
    if is_multilabel:
        y_idx = y_arr  # (N, L)
        classes = None
        class_to_idx = None
    else:
        # ensure numeric codes for plotting; keep mapping to encode predictions
        y_idx, classes, class_to_idx = _encode_labels_for_plot(y_arr)

    if d == 1:
        x = X[:, 0]
        x_min, x_max = float(x.min()), float(x.max())
        xs = np.linspace(
            x_min - 0.05 * (x_max - x_min + 1e-9),
            x_max + 0.05 * (x_max - x_min + 1e-9),
            400, dtype=np.float32
        )
        Xgrid = xs.reshape(-1, 1)
        S, C_eff, mode = _predict_scores_or_probs(model, Xgrid)

        if is_multilabel:
            counts = y_idx.sum(axis=1)
            plt.scatter(x, np.zeros_like(x), c=counts, s=28, edgecolors="k", cmap="viridis")
            plt.ylabel("#labels")
        else:
            plt.scatter(x, np.zeros_like(x), c=y_idx.reshape(-1), s=28, edgecolors="k")

        if mode == "prob":
            plt.plot(xs, S, linewidth=2)
            plt.axhline(0.5, color="red", linewidth=2)
            plt.ylim(-0.05, 1.05); plt.ylabel("P(class=1)")
        elif mode == "probs":
            plt.plot(xs, S.max(axis=1), linewidth=2)
            plt.ylim(-0.05, 1.05); plt.ylabel("Max class probability")
        plt.xlabel(feature_names[0] if feature_names else "x")

    elif d == 2:
        x0, x1 = X[:, 0], X[:, 1]
        xpad = (x0.max()-x0.min()+1e-9)*0.07; ypad=(x1.max()-x1.min()+1e-9)*0.07
        xx, yy = np.meshgrid(
            np.linspace(float(x0.min()-xpad), float(x0.max()+xpad), 300, dtype=np.float32),
            np.linspace(float(x1.min()-ypad), float(x1.max()+ypad), 300, dtype=np.float32)
        )
        grid = np.c_[xx.ravel(), yy.ravel()]
        S, C_eff, mode = _predict_scores_or_probs(model, grid)

        if is_multilabel:
            ax1 = plt.subplot(1,2,1)
            ax2 = plt.subplot(1,2,2)
            ax1.scatter(x0, x1, c=y_idx.sum(axis=1), s=18, cmap="viridis", alpha=0.9)
            ax1.set_title("True (#labels)"); ax1.set_xlabel("x1"); ax1.set_ylabel("x2")
            try:
                yp = np.asarray(model.predict(X))
            except Exception:
                if mode == "prob": yp = (S.reshape(-1) >= 0.5).astype(int)[:,None]
                elif mode == "probs":
                    pred_single = S.argmax(1); yp = np.zeros_like(y_idx)
                    for i in range(yp.shape[1]): yp[:, i] = (pred_single == i).astype(int)
                else:
                    yp = np.zeros_like(y_idx)
            corr = (yp == y_idx).all(axis=1).astype(int)
            ax2.scatter(x0, x1, c=corr, s=18, cmap="coolwarm", vmin=0, vmax=1, alpha=0.9)
            ax2.set_title("Pred correctness"); ax2.set_xlabel("x1"); ax2.set_ylabel("x2")
        else:
            # decision background
            if C_eff == 2 and mode in {"prob","probs"}:
                P = S if mode == "prob" else S[:,1]; P = P.reshape(xx.shape)
                plt.contourf(xx, yy, P, levels=20, alpha=0.55, cmap="RdBu")
                plt.contour(xx, yy, P, levels=[0.5], linewidths=2, colors="red")
            elif mode == "probs":
                cls = S.argmax(1).reshape(xx.shape)
                cmap = ListedColormap(plt.cm.tab10.colors[:int(C_eff)])
                plt.contourf(xx, yy, cls, levels=np.arange(-0.5, C_eff, 1), alpha=0.25, cmap=cmap)

            # points (numeric y_idx)
            plt.scatter(x0, x1, c=y_idx.reshape(-1), s=14, edgecolors="k")
            plt.xlabel(feature_names[0] if feature_names else "x1")
            plt.ylabel(feature_names[1] if feature_names and len(feature_names) > 1 else "x2")

    else:
        # High-D → project and show True vs Predicted classes (or correctness for multilabel)
        Xp0, Xp1 = project_2d(X, method=proj)
        ax1 = plt.subplot(1,2,1)
        ax2 = plt.subplot(1,2,2)
        if is_multilabel:
            ax1.scatter(Xp0, Xp1, c=y_idx.sum(axis=1), s=16, cmap="viridis", alpha=0.9)
            ax1.set_title("True (#labels)")
            try:
                yp = np.asarray(model.predict(X))
            except Exception:
                S, C_eff, mode = _predict_scores_or_probs(model, X)
                if mode == "prob": yp = (S.reshape(-1) >= 0.5).astype(int)[:,None]
                elif mode == "probs":
                    pred_single = S.argmax(1); yp = np.zeros_like(y_idx)
                    for i in range(yp.shape[1]): yp[:, i] = (pred_single == i).astype(int)
                else:
                    yp = np.zeros_like(y_idx)
            corr = (yp == y_idx).all(axis=1).astype(int)
            ax2.scatter(Xp0, Xp1, c=corr, s=16, cmap="coolwarm", vmin=0, vmax=1, alpha=0.9)
            ax2.set_title("Pred correctness")
        else:
            ax1.scatter(Xp0, Xp1, c=y_idx.reshape(-1), s=16, cmap="tab10", alpha=0.9)
            ax1.set_title("True classes")
            # predict in original feature space, then encode with same mapping
            try:
                yp = np.asarray(model.predict(X))
            except Exception:
                S, C_eff, mode = _predict_scores_or_probs(model, X)
                yp = S.argmax(1) if mode == "probs" else (S.reshape(-1)>=0.5).astype(int)
            if yp.dtype.kind in "OUS":
                yp = np.vectorize(lambda s: class_to_idx.get(str(s), -1))(yp).astype(int)
            else:
                yp = yp.astype(int)
            ax2.scatter(Xp0, Xp1, c=yp, s=16, cmap="tab10", alpha=0.9)
            ax2.set_title("Predicted classes")
        ax1.set_xlabel("PC1"); ax1.set_ylabel("PC2")
        ax2.set_xlabel("PC1"); ax2.set_ylabel("PC2")

    tmp = out_path + ".tmp.png"
    plt.tight_layout(rect=[0,0,1,0.95])
    plt.savefig(tmp, dpi=110, bbox_inches="tight")
    try:
        import os
        if os.path.exists(out_path):
            os.remove(out_path)
        os.replace(tmp, out_path)
    except Exception:
        pass

# --- Win95-ish plotting (kept) ------------------------------
def _retro95_bevel(fig):
    x0, x1, y0, y1 = 0.01, 0.99, 0.01, 0.99
    fig.lines.extend([
        plt.Line2D([x0,x1],[y1,y1], transform=fig.transFigure, lw=2, color="#404040"),
        plt.Line2D([x0,x1],[y0,y0], transform=fig.transFigure, lw=2, color="#FFFFFF"),
        plt.Line2D([x0,x0],[y0,y1], transform=fig.transFigure, lw=2, color="#FFFFFF"),
        plt.Line2D([x1,x1],[y0,y1], transform=fig.transFigure, lw=2, color="#404040"),
    ])

def _retro95_axes(ax, xlim=None, ylim=None, hide_spines=True):
    fig = ax.figure
    fig.patch.set_facecolor("#C0C0C0")
    ax.set_facecolor("#E5E5E5")
    if hide_spines:
        for sp in ax.spines.values():
            sp.set_visible(False)
    ax.grid(True, color="#AFAFAF", linewidth=0.9)
    ax.tick_params(colors="#000000", labelsize=9)
    if xlim: ax.set_xlim(*xlim)
    if ylim: ax.set_ylim(*ylim)
    _retro95_bevel(fig)

def _retro95_palette():
    return ["#0044AA","#FF00AA","#00AA00","#C00000",
            "#E1A500","#6B4E16","#7A1FA2","#008B8B",
            "#000000","#666666"]

def _quantize_0_100(arr):
    arr = np.asarray(arr, dtype=float) * 100.0
    return np.clip(np.rint(arr), 0, 100)

def _win95_axes(ax):
    fig = ax.figure
    fig.patch.set_facecolor("#C0C0C0")
    ax.set_facecolor("#E5E5E5")
    for spine in ax.spines.values():
        spine.set_visible(False)
    x0,x1 = 0.02, 0.98
    y0,y1 = 0.02, 0.98
    fig.lines.extend([
        plt.Line2D([x0,x1],[y1,y1], transform=fig.transFigure, lw=2, color="#404040"),
        plt.Line2D([x0,x1],[y0,y0], transform=fig.transFigure, lw=2, color="#FFFFFF"),
        plt.Line2D([x0,x0],[y0,y1], transform=fig.transFigure, lw=2, color="#FFFFFF"),
        plt.Line2D([x1,x1],[y0,y1], transform=fig.transFigure, lw=2, color="#404040"),
    ])
    ax.grid(True, color="#AFAFAF", alpha=0.6, linewidth=0.8)
    ax.tick_params(colors="#000000")
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100)

def save_plot_win95_timeseries(values_0_1, epoch, epochs, out_path, metric_label="Metric"):
    yy = _quantize_0_100(values_0_1)
    xx = np.linspace(0, 100, len(yy), dtype=float)

    fig = plt.figure(figsize=(8.5, 6.2), dpi=110)
    ax  = fig.add_axes([0.04, 0.12, 0.94, 0.82])
    _retro95_axes(ax, xlim=(0,100), ylim=(0,100))

    ax.fill_between(xx, 0, yy, step="post",
                    facecolor="#00FFFF", edgecolor="#006B6B",
                    linewidth=1.2, antialiased=False)
    ax.plot(xx, yy, drawstyle="steps-post",
            color="#006B6B", linewidth=1.4, antialiased=False)

    ax.set_title(f"Fitting — epoch {epoch}/{epochs}  |  {metric_label}", fontsize=12)
    plt.tight_layout(pad=0.6)

    tmp = out_path + ".tmp.png"
    plt.savefig(tmp, dpi=110, bbox_inches="tight")
    plt.close(fig)
    os.replace(tmp, out_path)

def _retro95_datafit(ax, X, y_info, model, is_clf, feat_names, classes=None, proj="pca2"):
    _retro95_axes(ax)
    X = np.asarray(X, dtype=np.float32)

    # Decide view: keep native 1D/2D; if >2D we PROJECT to 2D for visualization
    projected = False
    if X.shape[1] > 2:
        x0, x1 = project_2d(X, method=("pca2" if proj == "pca2" else ("tsne2" if proj == "tsne2" else "none")))
        XY = np.c_[x0, x1].astype(np.float32)
        xlabel, ylabel = ("PC1", "PC2") if proj != "none" else ("x1", "x2")
        projected = True
    elif X.shape[1] == 2:
        XY = X.astype(np.float32)
        xlabel = feat_names[0] if feat_names and len(feat_names) > 0 else "x1"
        ylabel = feat_names[1] if feat_names and len(feat_names) > 1 else "x2"
    else:
        XY = X.astype(np.float32)  # 1D case handled below
        xlabel = feat_names[0] if feat_names else "x"
        ylabel = ""

    if is_clf:
        y_idx, _, _ = _encode_labels_for_plot(y_info)
        pal = _retro95_palette()
        cmap = ListedColormap(pal[:max(2, int(y_idx.max()) + 1)])

    # ---------------- 1D ----------------
    if XY.shape[1] == 1:
        x = XY[:, 0]
        x_min, x_max = float(x.min()), float(x.max())
        xs = np.linspace(x_min, x_max, 400, dtype=np.float32).reshape(-1, 1)

        if is_clf:
            S, C_eff, mode = _predict_scores_or_probs(model, xs)
            if mode == "probs" and C_eff > 2:
                P = S.max(1)
            else:
                P = S.reshape(-1)
            ax.fill_between(xs[:, 0], -0.5, 0.5, where=(P >= 0.5),
                            step="post", color="#FFAACF", alpha=0.35, antialiased=False)
            ax.fill_between(xs[:, 0], -0.5, 0.5, where=(P < 0.5),
                            step="post", color="#99BBFF", alpha=0.35, antialiased=False)
            rng = np.random.RandomState(0)
            jitter = (rng.randn(len(x)) * 0.08)
            ax.scatter(x, jitter, c=y_idx, cmap=cmap, vmin=0, vmax=cmap.N-1,
                       s=26, marker="s", edgecolors="black", linewidths=0.6, alpha=0.95)
            ax.set_ylim(-0.9, 0.9)
        else:
            ax.scatter(x, np.zeros_like(x), s=26, c="#0044AA", edgecolors="black",
                       marker="s", alpha=0.9, linewidths=0.6)
            order = np.argsort(x)
            try:
                yhat = np.asarray(model.predict(x[order].reshape(-1, 1))).reshape(-1)
            except Exception:
                with torch.no_grad():
                    yhat = model(torch.from_numpy(x[order].reshape(-1, 1)).float()).cpu().numpy().reshape(-1)
            ax.plot(x[order], yhat, color="#C00000", linewidth=2.2, antialiased=False)

        ax.set_xlabel(xlabel);  ax.set_ylabel(ylabel)
        return

    # ---------------- 2D ----------------
    x, y = XY[:, 0], XY[:, 1]
    if is_clf:
        # Always scatter the data
        ax.scatter(x, y, c=y_idx, cmap=cmap, vmin=0, vmax=cmap.N-1,
                   s=22, marker="s", edgecolors="black", linewidths=0.5, alpha=0.95)

        # If this is a true 2D feature space, draw a decision surface.
        # If it's a PROJECTION from higher-D, do NOT query the model on a 2D grid (dims mismatch).
        if not projected:
            xpad = (x.max() - x.min() + 1e-9) * 0.07
            ypad = (y.max() - y.min() + 1e-9) * 0.07
            xx, yy = np.meshgrid(
                np.linspace(float(x.min()-xpad), float(x.max()+xpad), 240, dtype=np.float32),
                np.linspace(float(y.min()-ypad), float(y.max()+ypad), 240, dtype=np.float32)
            )
            grid = np.c_[xx.ravel(), yy.ravel()].astype(np.float32)

            S, C_eff, mode = _predict_scores_or_probs(model, grid)
            if C_eff > 2 and mode == "probs":
                cls = S.argmax(1).reshape(xx.shape)
                ax.pcolormesh(xx, yy, cls, cmap=cmap, alpha=0.20, shading="nearest")
            elif mode in {"prob","probs"}:
                P = S if mode == "prob" else S[:,1]
                P = P.reshape(xx.shape)
                ax.contour(xx, yy, P, levels=[0.5], colors="#C00000", linewidths=2.0, antialiased=False)
        else:
            # Optional bonus: outline misclassified points in the projected view
            try:
                Sx, Ce, md = _predict_scores_or_probs(model, np.asarray(X, dtype=np.float32))
                if md == "probs":
                    yp = Sx.argmax(1)
                elif md == "prob":
                    yp = (Sx.reshape(-1) >= 0.5).astype(int)
                else:
                    yp = np.asarray(model.predict(np.asarray(X, dtype=np.float32))).reshape(-1)
                err = (yp.reshape(-1) != y_idx.reshape(-1))
                if err.any():
                    ax.scatter(x[err], y[err], facecolors='none', edgecolors='black',
                               linewidths=1.2, s=64, marker='o', alpha=0.9)
            except Exception:
                pass

    else:
        ax.scatter(x, y, s=22, c="#0044AA", marker="s",
                   edgecolors="black", linewidths=0.5, alpha=0.9)

    ax.set_xlabel(xlabel);  ax.set_ylabel(ylabel)

    if is_clf and classes is not None:
        from matplotlib.lines import Line2D
        uniq = np.unique(y_idx)
        handles = [Line2D([0],[0], marker='s', color='none',
                          markerfacecolor=_retro95_palette()[int(i)%len(_retro95_palette())],
                          markeredgecolor='black', markersize=7,
                          label=str(classes[int(i)])) for i in uniq]
        ax.legend(handles=handles, loc='upper right', frameon=False, fontsize=8)

import time, os

def _safe_replace(tmp_path: str, out_path: str, retries: int = 25, delay: float = 0.03) -> None:
    last_err = None
    for _ in range(max(1, retries)):
        try:
            os.replace(tmp_path, out_path)
            return
        except PermissionError as e:
            last_err = e
            time.sleep(delay)
        except OSError as e:
            last_err = e
            time.sleep(delay)
    try:
        os.remove(tmp_path)
    except Exception:
        pass
    if last_err:
        raise last_err

def save_plot_combo_retro95(values_0_1, epoch, epochs, X, y_plot_info,
                            model, is_clf, feat_names, out_path, metric_label,
                            classes=None, proj="pca2"):
    fig = plt.figure(figsize=(7.8, 5.6), dpi=110)
    ax_top = fig.add_axes([0.04, 0.61, 0.94, 0.33])
    ax_bot = fig.add_axes([0.04, 0.10, 0.94, 0.44])

    yy = _quantize_0_100(values_0_1)
    xx = np.linspace(0, 100, len(yy), dtype=float)
    _retro95_axes(ax_top, xlim=(0,100), ylim=(0,100))
    ax_top.fill_between(xx, 0, yy, step="post",
                        facecolor="#00FFFF", edgecolor="#006B6B",
                        linewidth=1.2, antialiased=False)
    ax_top.plot(xx, yy, drawstyle="steps-post",
                color="#006B6B", linewidth=1.4, antialiased=False)
    ax_top.set_title(f"Fitting — epoch {epoch}/{epochs}  |  {metric_label}", fontsize=12)
    ax_top.set_xticklabels([]); ax_top.set_xlabel(""); ax_top.set_ylabel("")

    _retro95_datafit(ax_bot, X, y_plot_info, model, is_clf, feat_names, classes=classes, proj=proj)

    plt.tight_layout(pad=0.2)

    tmp = out_path + ".tmp.png"
    fig.subplots_adjust(left=0, right=1, top=1, bottom=0)
    plt.savefig(tmp, dpi=100, facecolor=fig.get_facecolor(),
                edgecolor=fig.get_facecolor(), bbox_inches=None, pad_inches=0)
    plt.close(fig)

    _safe_replace(tmp, out_path)

# --------- simple Levenshtein helpers (kept) ----------
def lev(a: str, b: str) -> int:
    n, m = len(a), len(b)
    dp = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n + 1):
        dp[i][0] = i
    for j in range(m + 1):
        dp[0][j] = j
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            if a[i - 1] == b[j - 1]:
                dp[i][j] = dp[i - 1][j - 1]
            else:
                dp[i][j] = 1 + min(dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1])
    return dp[n][m]

def lev_search(a: list, b: str):
    full = {i: lev(i, b) for i in a}
    return min(full, key=lambda k: full[k])

# -------------------- data treatment --------------------
def build_preprocessor(dfX: "pd.DataFrame", scale: str, impute: str, onehot: bool) -> ColumnTransformer:
    num_cols = [c for c in dfX.columns if pd.api.types.is_numeric_dtype(dfX[c])]
    cat_cols = [c for c in dfX.columns if c not in num_cols]

    imputer_map = {"mean": "mean", "median": "median", "most_frequent": "most_frequent", "zero": "constant"}
    strategy = imputer_map.get(impute, "mean")

    scaler = {"minmax": MinMaxScaler(),
              "standard": StandardScaler(with_mean=True, with_std=True),
              "none": "passthrough"}.get(scale, StandardScaler())

    transformers = []
    if num_cols:
        if strategy == "constant":
            num_pipe = Pipeline([("imputer", SimpleImputer(strategy="constant", fill_value=0.0)),
                                 ("scaler", scaler)])
        else:
            num_pipe = Pipeline([("imputer", SimpleImputer(strategy=strategy)),
                                 ("scaler", scaler)])
        transformers.append(("num", num_pipe, num_cols))
    if cat_cols:
        steps: List[Tuple[str, Any]] = []
        if strategy == "constant":
            steps.append(("imputer", SimpleImputer(strategy="constant", fill_value="")))
        else:
            steps.append(("imputer", SimpleImputer(strategy="most_frequent")))
        if onehot:
            steps.append(("onehot", OneHotEncoder(handle_unknown="ignore", sparse=False)))
        transformers.append(("cat", Pipeline(steps), cat_cols))

    if not transformers:
        return ColumnTransformer([("id", "passthrough", list(dfX.columns))])
    return ColumnTransformer(transformers)

# ---------- simple NB "regressor" via binning (to satisfy nb_reg variant) ----
class GaussianNBRegressor:
    def __init__(self, n_bins: int = 10):
        self.n_bins = int(max(2, n_bins))
        self.clf = GaussianNB()
        self.edges_: Optional[np.ndarray] = None
        self.bin_means_: Optional[np.ndarray] = None
    def fit(self, X, y):
        y = np.asarray(y).reshape(-1)
        qs = np.linspace(0.0, 1.0, self.n_bins + 1)
        edges = np.quantile(y, qs)
        edges = np.unique(edges)
        if len(edges) < 3:
            edges = np.linspace(y.min(), y.max(), self.n_bins + 1)
        self.edges_ = edges
        idx = np.clip(np.digitize(y, edges[1:-1], right=True), 0, len(edges) - 2)
        means = np.zeros(len(edges) - 1, dtype=float)
        for b in range(len(means)):
            sel = idx == b
            means[b] = y[sel].mean() if sel.any() else (edges[b] + edges[b+1]) / 2.0
        self.bin_means_ = means
        self.clf.fit(X, idx)
        return self
    def predict(self, X):
        b = self.clf.predict(X).astype(int)
        return self.bin_means_[b]

def print_classification_report(y_true_idx, y_pred_idx, classes, stream=None):
    """
    Pretty text report (and confusion matrix) for single-label classification.
    - y_true_idx, y_pred_idx: 1-D integer arrays (0..C-1) OR raw labels; we coerce safely.
    - classes: list/array of class names in index order.
    - stream: file-like to write into; if None -> print().
    Returns the assembled string.
    """
    import numpy as _np
    y_true = _np.asarray(y_true_idx)
    y_pred = _np.asarray(y_pred_idx)
    cls    = _np.asarray(list(classes))

    # If labels aren’t integers 0..C-1, coerce them together to indices.
    if y_true.dtype.kind in "OUS" or y_pred.dtype.kind in "OUS":
        all_labels = _np.unique(_np.concatenate([y_true.astype(str), y_pred.astype(str)]))
        map2 = {s:i for i, s in enumerate(all_labels)}
        y_true = _np.vectorize(map2.get)(y_true.astype(str)).astype(int)
        y_pred = _np.vectorize(map2.get)(y_pred.astype(str)).astype(int)
        cls    = all_labels  # real names
    else:
        y_true = y_true.reshape(-1).astype(int)
        y_pred = y_pred.reshape(-1).astype(int)

    C = int(max(cls.size, y_true.max(initial=-1)+1, y_pred.max(initial=-1)+1))
    if cls.size != C:
        # pad class names if needed
        names = list(map(str, cls.tolist()))
        names += [f"class_{i}" for i in range(len(names), C)]
        cls = _np.asarray(names)

    lines = []

    # Try scikit-learn for detailed metrics; otherwise fallback to a manual summary.
    try:
        from sklearn.metrics import classification_report, confusion_matrix, accuracy_score
        rep = classification_report(
            y_true, y_pred,
            labels=_np.arange(C),
            target_names=[str(s) for s in cls],
            digits=4,
            zero_division=0,
        )
        cm = confusion_matrix(y_true, y_pred, labels=_np.arange(C))
        acc = accuracy_score(y_true, y_pred)
        lines.append("=== Classification Report ===")
        lines.append(rep.rstrip())
    except Exception:
        # Manual confusion + per-class precision/recall/F1
        cm = _np.zeros((C, C), dtype=int)
        for t, p in zip(y_true, y_pred):
            if 0 <= t < C and 0 <= p < C:
                cm[t, p] += 1
        acc = (cm.trace() / max(1, cm.sum())).item()
        lines.append("=== Classification Report (fallback) ===")
        lines.append(f"accuracy: {acc:.4f}")
        # per-class
        header = f"{'class':<18} {'prec':>7} {'rec':>7} {'f1':>7} {'support':>8}"
        lines.append(header)
        for i in range(C):
            tp = cm[i, i]
            fp = cm[:, i].sum() - tp
            fn = cm[i, :].sum() - tp
            sup = cm[i, :].sum()
            prec = tp / (tp + fp + 1e-12)
            rec  = tp / (tp + fn + 1e-12)
            f1   = 2 * prec * rec / (prec + rec + 1e-12)
            lines.append(f"{str(cls[i]):<18} {prec:7.4f} {rec:7.4f} {f1:7.4f} {sup:8d}")

    # Append an ASCII confusion matrix
    # formatting
    namew = max(5, max(len(str(s)) for s in cls))
    cellw = max(3, len(str(cm.max(initial=0))))
    lines.append("")
    lines.append("Confusion Matrix (rows=true, cols=pred):")
    row0 = " " * (namew + 2) + " ".join(f"{str(s):>{cellw}}" for s in cls)
    lines.append(row0)
    for i in range(C):
        row = f"{str(cls[i]):>{namew}} | " + " ".join(f"{cm[i, j]:>{cellw}d}" for j in range(C))
        lines.append(row)
    lines.append("")
    lines.append(f"Overall accuracy: {acc:.4f}")

    text = "\n".join(lines)
    if stream is not None:
        stream.write(text)
        stream.flush()
    else:
        print(text, flush=True)
    return text

# -------------------- main --------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", required=True)
    ap.add_argument("--x", "--x-col", dest="x", required=True)   # one or more, comma separated
    ap.add_argument("--y", "--y-col", dest="y", required=True)   # allow multi (comma) for multilabel/multioutput
    ap.add_argument("--x-label", type=str, default=None)
    ap.add_argument("--y-label", type=str, default=None)

    # The model CHOOSES THE TASK. (kept + extended choices)
    ap.add_argument("--model",
                    choices=[
                        "linreg","ridge","lasso","mlp_reg",             # torch/reg
                        "logreg","mlp_cls",                              # torch/clf
                        # classical (sklearn) — classification:
                        "dt_cls","rf_cls","knn_cls","nb_cls","svm_cls","gb_cls",
                        # classical — regression:
                        "dt_reg","rf_reg","knn_reg","nb_reg","svm_reg","gb_reg"
                    ],
                    default="linreg")
    ap.add_argument("--epochs", type=int, default=100)
    ap.add_argument("--train-pct", type=float, default=0.70)
    ap.add_argument("--proj", choices=["none","pca2","tsne2"], default="pca2")
    ap.add_argument("--color-by", type=str, default="residual")
    ap.add_argument("--frame-every", type=int, default=1)
    ap.add_argument("--out-plot", default="")
    ap.add_argument("--out-metrics", default="")
    ap.add_argument("--plot-every", type=int, default=1)
    ap.add_argument("--plot-style", choices=["modern", "retro95"], 
                    default=os.environ.get("AIFD_PLOT_STYLE", "modern"))
    ap.add_argument("--hparams", type=str, default="")
    # data treatment
    ap.add_argument("--scale", default="standard", choices=["none","standard","minmax"])
    ap.add_argument("--impute", default="mean", choices=["mean","median","most_frequent","zero"])
    ap.add_argument("--onehot", action="store_true")

    args = ap.parse_args()

    hp = {}
    if args.hparams:
        try:
            hp = json.loads(args.hparams)
        except Exception as e:
            print(f"[warn] invalid --hparams JSON: {e}", flush=True)
            hp = {}

    if pd is None:
        raise SystemExit("pandas is required to load CSVs")

    df = pd.read_csv(args.csv)
    feat_names = [s.strip() for s in args.x.split(",") if s.strip()]
    y_feats    = [s.strip() for s in args.y.split(",") if s.strip()]
    df_cols = list(df.columns)

    fixed_feat_names = []
    for name in feat_names:
        fixed_feat_names.append(name if name in df_cols else lev_search(df_cols, name))
    feat_names = fixed_feat_names

    fixed_y_feats = []
    for name in y_feats:
        fixed_y_feats.append(name if name in df_cols else lev_search(df_cols, name))
    y_feats = fixed_y_feats

    dfX = df[feat_names].copy()
    dfY = df[y_feats].copy()

    # ---- data treatment (applied to X only; we keep y as-is) ----
    if _SK_OK:
        pre = build_preprocessor(dfX, args.scale, args.impute, args.onehot)
        X = np.asarray(pre.fit_transform(dfX), dtype=np.float32)
        X_feature_names = feat_names  # after onehot we lose names; keep originals for labels
    else:
        X = dfX.to_numpy(dtype=np.float32)
        X_feature_names = feat_names

    Y = dfY.to_numpy()
    # decide multilabel: multiple y columns and all values in {0,1}
    is_multilabel = (Y.ndim == 2 and Y.shape[1] > 1 and set(np.unique(Y[~np.isnan(Y)])).issubset({0,1}))

    # Decide task strictly from model (preserves old behavior for torch models).
    torch_cls = {"logreg","mlp_cls"}
    torch_reg = {"linreg","ridge","lasso","mlp_reg"}
    sk_cls    = {"dt_cls","rf_cls","knn_cls","nb_cls","svm_cls","gb_cls"}
    sk_reg    = {"dt_reg","rf_reg","knn_reg","nb_reg","svm_reg","gb_reg"}

    is_clf_model = args.model in (torch_cls | sk_cls)

    # Small debug line (you already had this)
    print(f"[dbg] task={'clf' if is_clf_model else 'reg'}  dim={X.shape[1]}  x='{args.x}'  y='{args.y}'", flush=True)

    # Split
    Xtr, Xte, ytr, yte = train_test_split(X, Y, args.train_pct, seed=123)
    in_dim = X.shape[1]

    # ---------------------- BUILD / TRAIN ----------------------
    device = torch.device("cpu")
    plot_style = args.plot_style
    hist_vals = []         # metric 0..1
    metric_label = ""      # legend

    # ---- helper: metrics printing blocks (reused) ----
    def print_multilabel_metrics(y_true, y_pred) -> str:
        if not _SK_OK:
            txt = "sklearn not available; cannot compute multilabel F1"
            print(txt); return txt
        micro_f1 = f1_score(y_true, y_pred, average="micro", zero_division=0)
        macro_f1 = f1_score(y_true, y_pred, average="macro", zero_division=0)
        txt = [
            "=== Multilabel Metrics ===",
            f"F1 (micro): {micro_f1:.6f}",
            f"F1 (macro): {macro_f1:.6f}",
        ]
        out = "\n".join(txt)
        print("\n"+out, flush=True)
        return out

    # ---- classical sklearn models ----
    if args.model in (sk_cls | sk_reg):
        if not _SK_OK:
            raise SystemExit("Requested classical model but scikit-learn is not available.")

        # construct model from hp
        m = args.model
        if m == "dt_cls":
            base = DecisionTreeClassifier(random_state=int(hp.get("seed", 42)))
            model = OneVsRestClassifier(base) if is_multilabel else base
        elif m == "dt_reg":
            model = DecisionTreeRegressor(random_state=int(hp.get("seed", 42)))
        elif m == "rf_cls":
            base = RandomForestClassifier(
                n_estimators=int(hp.get("n_estimators", 200)),
                max_depth=hp.get("max_depth", None),
                random_state=int(hp.get("seed", 42)),
                n_jobs=-1
            )
            model = OneVsRestClassifier(base) if is_multilabel else base
        elif m == "rf_reg":
            model = RandomForestRegressor(
                n_estimators=int(hp.get("n_estimators", 200)),
                max_depth=hp.get("max_depth", None),
                random_state=int(hp.get("seed", 42)),
                n_jobs=-1
            )
        elif m == "knn_cls":
            base = KNeighborsClassifier(n_neighbors=int(hp.get("n_neighbors", 7)))
            model = OneVsRestClassifier(base) if is_multilabel else base
        elif m == "knn_reg":
            model = KNeighborsRegressor(n_neighbors=int(hp.get("n_neighbors", 7)))
        elif m == "nb_cls":
            base = GaussianNB()
            model = OneVsRestClassifier(base) if is_multilabel else base
        elif m == "nb_reg":
            model = GaussianNBRegressor(n_bins=int(hp.get("nb_reg_bins", 10)))
        elif m == "svm_cls":
            base = SVC(kernel=hp.get("kernel", "rbf"),
                       C=float(hp.get("C", 1.0)),
                       gamma=hp.get("gamma", "scale"),
                       probability=True,
                       random_state=int(hp.get("seed", 42)))
            model = OneVsRestClassifier(base) if is_multilabel else base
        elif m == "svm_reg":
            model = SVR(kernel=hp.get("kernel", "rbf"),
                        C=float(hp.get("C", 1.0)),
                        gamma=hp.get("gamma", "scale"),
                        epsilon=float(hp.get("epsilon", 0.1)))
        elif m == "gb_cls":
            base = GradientBoostingClassifier(random_state=int(hp.get("seed", 42)))
            model = OneVsRestClassifier(base) if is_multilabel else base
        elif m == "gb_reg":
            model = GradientBoostingRegressor(random_state=int(hp.get("seed", 42)))
        else:
            raise SystemExit(f"Unknown model {m}")

        # fit once
        ytr_fit = ytr if is_multilabel else ytr.reshape(-1)
        model = model.fit(Xtr, ytr_fit)
        # plots (single frame at the end, to keep changes minimal)
        if args.out_plot:
            if is_clf_model:
                save_plot_classification(Xtr, ytr, model, args.epochs, args.epochs, args.out_plot,
                                         feature_names=X_feature_names, proj=args.proj)
            else:
                y_for_plot = ytr.astype(float) if ytr.ndim == 1 else ytr[:,0].astype(float)
                save_plot_regression(Xtr, y_for_plot, model, args.epochs, args.epochs, args.out_plot,
                                     x_label=(args.x_label or X_feature_names[0] if len(X_feature_names)==1 else "X"),
                                     y_label=(args.y_label or ",".join(y_feats)), proj=args.proj, color_by=args.color_by)

        # test + metrics
        if is_clf_model:
            yhat = np.asarray(model.predict(Xte))
            if is_multilabel:
                text = print_multilabel_metrics(yte, yhat)
            else:
                # Encode true/pred labels together so plotting/metrics can use numeric indices.
                y_true_raw = yte.reshape(-1)
                y_pred_raw = yhat.reshape(-1)

                if (y_true_raw.dtype.kind in "OUS") or (y_pred_raw.dtype.kind in "OUS"):
                    # string/object labels like 'B'/'M'
                    y_true_str = y_true_raw.astype(str)
                    y_pred_str = y_pred_raw.astype(str)
                    classes = np.unique(np.concatenate([y_true_str, y_pred_str]))
                    class_to_idx = {c: i for i, c in enumerate(classes)}
                    ytrue_idx = np.vectorize(class_to_idx.get)(y_true_str).astype(int)
                    ypred_idx = np.vectorize(class_to_idx.get)(y_pred_str).astype(int)
                else:
                    # numeric labels
                    classes = np.unique(np.concatenate([y_true_raw, y_pred_raw]))
                    map_to_idx = {int(c): i for i, c in enumerate(classes)}
                    ytrue_idx = np.vectorize(map_to_idx.get)(y_true_raw.astype(int))
                    ypred_idx = np.vectorize(map_to_idx.get)(y_pred_raw.astype(int))

                from io import StringIO
                buf = StringIO()
                print_classification_report(ytrue_idx, ypred_idx, classes, stream=buf)
                text = buf.getvalue()
                print("\n"+text, flush=True)


        if args.out_metrics:
            tmp = args.out_metrics + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                f.write(text)
            os.replace(tmp, args.out_metrics)

        return  # classical path ends here

    # ---- torch models (kept logic; with small tweaks for multilabel) ----
    # BUILD MODEL / TARGETS
    if is_clf_model:
        # multilabel?
        if is_multilabel:
            n_labels = ytr.shape[1]
            hidden  = int(hp.get("hidden", max(8, in_dim * 2))) if args.model == "mlp_cls" else max(8, in_dim * 2)
            layers  = int(hp.get("layers", 2))
            actname = str(hp.get("activation", "relu")).lower()
            Act = nn.ReLU if actname == "relu" else nn.Tanh
            seq: list[nn.Module] = [nn.Linear(in_dim, hidden), Act()]
            for _ in range(max(0, layers - 1)):
                seq += [nn.Linear(hidden, hidden), Act()]
            seq += [nn.Linear(hidden, n_labels)]
            model = nn.Sequential(*seq).to(device)
            loss_fn = nn.BCEWithLogitsLoss()
            yt = torch.from_numpy(ytr.astype(np.float32))
            lr = float(hp.get("lr", 1e-3))
            opt = optim.Adam(model.parameters(), lr=lr)
        else:
            # single-label (kept)
            def encode_labels(y, classes=None):
                """
                Map arbitrary labels to contiguous indices [0..C-1].
                Accepts y as (N,), (N,1), pandas Series/DataFrame column, etc.
                """
                y = np.asarray(y)
                # Flatten to 1-D for single-label classification
                if y.ndim > 1:
                    if y.shape[1] == 1:
                        y = y[:, 0]
                    else:
                        raise ValueError(
                            "encode_labels expects 1-D labels. For multilabel, pass multiple --y "
                            "columns (0/1 each) so the code takes the multilabel path instead."
                        )

                y_str = y.astype(str)  # stable, even if original was mixed types
                if classes is None:
                    classes = np.unique(y_str)
                class_to_idx = {c: i for i, c in enumerate(classes)}
                # Vectorized map
                y_idx = np.vectorize(class_to_idx.get, otypes=[np.int64])(y_str)
                return y_idx.astype(np.int64), classes

            ytr_idx, classes = encode_labels(ytr)
            yte_idx, _       = encode_labels(yte, classes)
            ncls = len(classes)

            if args.model == "mlp_cls":
                hidden  = int(hp.get("hidden", max(8, in_dim * 2)))
                layers  = int(hp.get("layers", 2))
                actname = str(hp.get("activation", "relu")).lower()
                Act = nn.ReLU if actname == "relu" else nn.Tanh

                seq: list[nn.Module] = [nn.Linear(in_dim, hidden), Act()]
                for _ in range(max(0, layers - 1)):
                    seq += [nn.Linear(hidden, hidden), Act()]
                if ncls == 2:
                    seq += [nn.Linear(hidden, 1)]
                    loss_fn = nn.BCEWithLogitsLoss()
                    yt = torch.from_numpy(ytr_idx.astype(np.float32)).view(-1, 1)
                else:
                    seq += [nn.Linear(hidden, ncls)]
                    loss_fn = nn.CrossEntropyLoss()
                    yt = torch.from_numpy(ytr_idx.astype(np.int64))
                model = nn.Sequential(*seq).to(device)

                lr = float(hp.get("lr", 5e-2))
                opt = optim.Adam(model.parameters(), lr=lr)

            else:  # logistic regression
                C        = float(hp.get("C", 1.0))
                penalty  = str(hp.get("penalty", "L2")).upper()
                lr       = float(hp.get("lr", 5e-2))
                weight_decay = (1.0 / C) if penalty == "L2" else 0.0
                l1_lambda    = (1.0 / C) if penalty == "L1" else 0.0

                if ncls == 2:
                    model = nn.Sequential(nn.Linear(in_dim, 1)).to(device)
                    loss_fn = nn.BCEWithLogitsLoss()
                    yt = torch.from_numpy(ytr_idx.astype(np.float32)).view(-1, 1)
                else:
                    model = nn.Sequential(nn.Linear(in_dim, ncls)).to(device)
                    loss_fn = nn.CrossEntropyLoss()
                    yt = torch.from_numpy(ytr_idx.astype(np.int64))

                opt = optim.Adam(model.parameters(), lr=lr, weight_decay=weight_decay)

    else:
        # regression (kept)
        if args.model == "mlp_reg":
            hidden  = int(hp.get("hidden", max(16, in_dim * 2)))
            layers  = int(hp.get("layers", 2))
            actname = str(hp.get("activation", "relu")).lower()
            Act = nn.ReLU if actname == "relu" else nn.Tanh
            seq: list[nn.Module] = [nn.Linear(in_dim, hidden), Act()]
            for _ in range(max(0, layers - 1)):
                seq += [nn.Linear(hidden, hidden), Act()]
            seq += [nn.Linear(hidden, 1)]
            model = nn.Sequential(*seq).to(device)
            loss_fn = nn.MSELoss()
            lr = float(hp.get("lr", 5e-3))
            opt = optim.Adam(model.parameters(), lr=lr)
            yt = torch.from_numpy(ytr.astype(np.float32)).view(-1, 1)
        else:
            model = nn.Sequential(nn.Linear(in_dim, 1)).to(device)
            loss_fn = nn.MSELoss()
            alpha    = float(hp.get("alpha", 1e-2))
            lr       = float(hp.get("lr", 5e-2))
            weight_decay = alpha if args.model == "ridge" else 0.0
            l1_lambda   = alpha if args.model == "lasso" else 0.0
            opt = optim.Adam(model.parameters(), lr=lr, weight_decay=weight_decay)
            yt = torch.from_numpy(ytr.astype(np.float32)).view(-1, 1)

    Xt = torch.from_numpy(Xtr).float()

    # ----------------------------- TRAIN (torch) -------------------------------
    for epoch in range(1, args.epochs+1):
        opt.zero_grad()
        out = model(Xt)
        if is_clf_model and isinstance(loss_fn, nn.CrossEntropyLoss):
            loss = loss_fn(out, yt)
        else:
            loss = loss_fn(out, yt)

        # L1 for lasso/logreg (kept)
        if (not is_clf_model) and args.model == "lasso":
            l1 = sum(p.abs().sum() for p in model.parameters())
            loss = loss + float(hp.get("l1_lambda", l1_lambda if 'l1_lambda' in locals() else 0.0)) * l1
        if is_clf_model and args.model == "logreg" and float(hp.get("l1_lambda", 0.0)) > 0.0:
            l1 = sum(p.abs().sum() for p in model.parameters())
            loss = loss + float(hp["l1_lambda"]) * l1

        loss.backward(); opt.step()

        # metric 0..1 for retro95 top monitor
        with torch.no_grad():
            if is_clf_model:
                logits_tr = model(Xt)
                if is_multilabel:
                    yhat = (logits_tr >= 0.0).float()
                    # micro-F1-ish proxy: exact match isn't great; use mean per-label acc as proxy
                    acc = float((yhat.round() == yt).float().mean().cpu().item())
                    hist_vals.append(acc)
                    metric_label = f"Training multilabel (proxy acc): {acc*100:.1f}%"
                else:
                    if logits_tr.dim()==2 and logits_tr.shape[1]>1:
                        yhat_idx_tr = torch.argmax(logits_tr, dim=1).cpu().numpy()
                    else:
                        yhat_idx_tr = (torch.sigmoid(logits_tr.view(-1))>=0.5).cpu().numpy().astype(int)
                    ytrue_idx_tr = (yt if isinstance(loss_fn, nn.CrossEntropyLoss) else yt.view(-1)).cpu().numpy().astype(int)
                    acc_tr = float((yhat_idx_tr == ytrue_idx_tr).mean())
                    hist_vals.append(acc_tr)
                    metric_label = f"Training accuracy: {acc_tr*100:.1f}%"
            else:
                yhat_tr = model(Xt).cpu().numpy().reshape(-1)
                ytrue_tr = yt.view(-1).cpu().numpy().astype(float)
                ss_res = float(np.sum((ytrue_tr - yhat_tr)**2))
                ss_tot = float(np.sum((ytrue_tr - ytrue_tr.mean())**2)) or 1.0
                r2 = max(0.0, min(1.0, 1.0 - ss_res/ss_tot))
                hist_vals.append(r2)
                metric_label = f"Training R²: {r2*100:.1f}%"

        # render frames
        if args.out_plot and (epoch % max(1, args.frame_every) == 0 or epoch == args.epochs):
            if plot_style == "retro95":
                y_plot = (ytr if not is_clf_model else (ytr if is_multilabel else encode_labels(ytr)[0]))  # type: ignore
                save_plot_combo_retro95(
                    hist_vals, epoch, args.epochs, Xtr, y_plot,
                    model, is_clf_model, X_feature_names,
                    args.out_plot, metric_label,
                    classes=(None if is_multilabel else (encode_labels(ytr)[1] if is_clf_model else None)),
                    proj=args.proj
                )
            else:
                if is_clf_model:
                    y_for_plot = (ytr if is_multilabel else encode_labels(ytr)[0])  # type: ignore
                    save_plot_classification(
                        Xtr, y_for_plot, model, epoch, args.epochs, args.out_plot,
                        feature_names=X_feature_names, device="cpu", proj=args.proj
                    )
                else:
                    save_plot_regression(
                        Xtr, ytr.astype(float), model, epoch, args.epochs, args.out_plot,
                        x_label=(args.x_label or X_feature_names[0] if len(X_feature_names)==1 else "X"),
                        y_label=(args.y_label or ",".join(y_feats)), proj=args.proj, color_by=args.color_by)

        print(f"epoch {epoch}/{args.epochs}  loss={loss.item():.6f}", flush=True)

    # ------------------------ TEST + METRICS (torch) ---------------------------
    model.eval()
    with torch.no_grad():
        if is_clf_model:
            logits_te = model(torch.from_numpy(Xte).float())
            if is_multilabel:
                prob = torch.sigmoid(logits_te).cpu().numpy()
                yhat = (prob >= 0.5).astype(int)
                text = "=== Multilabel Metrics (proxy) ===\n" + \
                       f"Exact-match accuracy: {float((yhat==yte).all(axis=1).mean()):.6f}"
                print("\n"+text, flush=True)
            else:
                if logits_te.dim() == 2 and logits_te.shape[1] > 1:
                    prob = torch.softmax(logits_te, dim=1).cpu().numpy()
                    yhat_idx = np.argmax(prob, axis=1)
                else:
                    prob = torch.sigmoid(logits_te.view(-1)).cpu().numpy()
                    yhat_idx = (prob >= 0.5).astype(int)
        else:
            pred = model(torch.from_numpy(Xte).float()).cpu().numpy().squeeze()

    if is_clf_model and not is_multilabel:
        # detailed metrics + ASCII confusion (multi-class aware)
        from io import StringIO
        yte_idx = encode_labels(yte)[0]  # reuse your helper mapping
        classes = encode_labels(ytr)[1]
        buf = StringIO()
        print_classification_report(yte_idx, yhat_idx, classes, stream=buf)
        text = buf.getvalue()
        print("\n" + text, flush=True)
    elif not is_clf_model:
        r2, mae, mse, rmse = regression_metrics(yte, pred)
        text = "\n".join([
            "=== Regression Metrics ===",
            f"R^2  : {r2:.6f}",
            f"MAE  : {mae:.6f}",
            f"MSE  : {mse:.6f}",
            f"RMSE : {rmse:.6f}",
        ])
        print("\n" + text, flush=True)

    if args.out_metrics:
        tmp = args.out_metrics + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(text)
        os.replace(tmp, args.out_metrics)


if __name__ == "__main__":
    main()
