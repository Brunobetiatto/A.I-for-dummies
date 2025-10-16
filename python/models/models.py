# python/models.py
from __future__ import annotations
from typing import Tuple, List, Optional, Dict, Any, Union
from pathlib import Path
import os, sys, json, time, argparse

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
    torch.save({"state_dict": model.state_dict(), "meta": meta}, out_bin)
    try:
        (cache_path / f"model.{model_name}.meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    except Exception:
        pass
    return out_bin

def _train_and_cache(
    dataX: Tensorable, dataY: Tensorable, testX: Tensorable, testY: Tensorable,
    trainee: nn.Module, model_name: Optional[str] = None, cache_path: Path = CACHE_PATH,
    fit_params: Optional[Dict[str, Any]] = None, pause_file: Optional[Path] = None, **kwargs: Any,
) -> Tuple[nn.Module, float, Path]:
    fp = fit_params or {}
    task = fp.get("task", None)
    Xnp = _to_numpy(dataX);  Xnp = Xnp.reshape(-1, 1) if Xnp.ndim == 1 else Xnp

    def emit(**payload): print(json.dumps(payload), flush=True)

    emit(event="begin", task=task, input_dim=int(Xnp.shape[1]), params=fp)
    model, score = _train(
        dataX, dataY, testX, testY, trainee,
        fit_params=fp, pause_file=pause_file,
        progress_cb=lambda **p: emit(event="epoch", **p)
    )
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

def save_plot_regression(X, y, model, epoch, n_epochs, out_path,
                         x_label=None, y_label=None, proj="pca2", color_by="residual"):
    """
    Live regression plot.
    * 1-D: scatter of (x, y) with a RED fitted curve (sorted by x).
    * N-D: 2-D projection (PCA/t-SNE/none) colored by residuals.
    Writes atomically to out_path so the GTK side can reload safely.
    """
    X = np.asarray(X); y = np.asarray(y).reshape(-1)
    with torch.no_grad():
        yp = model(torch.from_numpy(X).float()).cpu().numpy().reshape(-1)
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


def encode_labels(y, classes=None):
    """
    Returns (y_idx, classes).
    If classes is None, it discovers unique classes from y (as strings).
    Otherwise it maps using the provided ordered 'classes' list/array.
    """
    y = np.asarray(y)
    # Make everything strings so we can handle ints/floats/objects uniformly
    y_str = y.astype(str)
    if classes is None:
        # preserve deterministic order by sorting unique values
        classes = np.unique(y_str)
    class_to_idx = {c: i for i, c in enumerate(classes)}
    y_idx = np.array([class_to_idx[s] for s in y_str], dtype=np.int64)
    return y_idx, classes

def print_classification_report(y_true_idx, y_pred_idx, classes, stream=None):
    """
    Prints accuracy/precision/recall/F1 (macro+micro) and an ASCII confusion matrix.
    """
    import sys
    out = stream if stream is not None else sys.stdout

    C = len(classes)
    cm = np.zeros((C, C), dtype=np.int64)
    for t, p in zip(y_true_idx, y_pred_idx):
        cm[t, p] += 1

    # per-class precision/recall/F1
    precs, recs, f1s = [], [], []
    for k in range(C):
        tp = cm[k, k]
        fp = cm[:, k].sum() - tp
        fn = cm[k, :].sum() - tp
        p = tp / (tp + fp) if (tp + fp) > 0 else 0.0
        r = tp / (tp + fn) if (tp + fn) > 0 else 0.0
        f = 2 * p * r / (p + r) if (p + r) > 0 else 0.0
        precs.append(p); recs.append(r); f1s.append(f)

    acc = float(np.trace(cm)) / float(cm.sum()) if cm.sum() > 0 else 0.0
    macro_p = float(np.mean(precs)) if C > 0 else 0.0
    macro_r = float(np.mean(recs))  if C > 0 else 0.0
    macro_f1 = float(np.mean(f1s))  if C > 0 else 0.0
    # For single-label multi-class, micro P/R/F1 equals accuracy.
    micro_p = micro_r = micro_f1 = acc

    print("\n=== Classification Metrics ===", file=out)
    print(f"Accuracy : {acc:.6f}", file=out)
    print(f"Precision (macro) : {macro_p:.6f}", file=out)
    print(f"Recall    (macro) : {macro_r:.6f}", file=out)
    print(f"F1        (macro) : {macro_f1:.6f}", file=out)
    print(f"Precision (micro) : {micro_p:.6f}", file=out)
    print(f"Recall    (micro) : {micro_r:.6f}", file=out)
    print(f"F1        (micro) : {micro_f1:.6f}", file=out)

    # ASCII confusion matrix
    w = max(7, max(len(str(c)) for c in classes) + 2)
    print("\nConfusion matrix (rows=true, cols=pred):", file=out)
    header = " " * w + "".join(f"{str(c):>{w}}" for c in classes)
    print(header, file=out)
    for i, c in enumerate(classes):
        row = f"{str(c):>{w}}" + "".join(f"{cm[i, j]:>{w}d}" for j in range(C))
        print(row, file=out)
    print("", file=out)

def save_plot_classification(X, y_idx, model, epoch, epochs, out_path,
                             feature_names=None, device="cpu"):
    """
    Saves either a 1D or 2D classification plot:
      - 1D: points along X and sigmoid/softmax region, red decision boundary (binary)
      - 2D: scatter + decision surface; red contour at p=0.5 for binary
    """
    X = np.asarray(X, dtype=np.float32)
    y_i = np.asarray(y_idx)
    d = X.shape[1]

    plt.clf()
    plt.figure(figsize=(7, 5), dpi=110)
    plt.title(f"Training epoch {epoch}/{epochs}")

    if d == 1:
        x = X[:, 0]
        x_min, x_max = float(x.min()), float(x.max())
        xs = np.linspace(
            x_min - 0.05 * (x_max - x_min + 1e-9),
            x_max + 0.05 * (x_max - x_min + 1e-9),
            400, dtype=np.float32
        )
        Xgrid = torch.from_numpy(xs.reshape(-1, 1)).to(device)

        with torch.no_grad():
            logits = model(Xgrid)

        # --- Infer class count & turn logits into probabilities ---
        if logits.dim() == 1:
            p = torch.sigmoid(logits).cpu().numpy()             # (N,)
            C_eff = 2
        elif logits.dim() == 2 and logits.shape[1] == 1:
            p = torch.sigmoid(logits[:, 0]).cpu().numpy()       # (N,)
            C_eff = 2
        elif logits.dim() == 2 and logits.shape[1] > 1:
            probs = torch.softmax(logits, dim=1).cpu().numpy()  # (N, C)
            p = probs.max(axis=1)                                # (N,)
            C_eff = logits.shape[1]
        else:
            # Fallback: treat as binary
            p = torch.sigmoid(logits.view(-1)).cpu().numpy()
            C_eff = 2

        # scatter points on a baseline, colored by class
        plt.scatter(x, np.zeros_like(x), c=y_i, s=28, edgecolors="k")

        # probability curve
        plt.plot(xs, p, linewidth=2)
        if C_eff == 2:
            plt.axhline(0.5, color="red", linewidth=2)          # decision threshold
            plt.ylabel("P(class=1)")
        else:
            plt.ylabel("Max class probability")
        plt.ylim(-0.05, 1.05)
        plt.xlabel(feature_names[0] if feature_names else "x")

    elif d == 2:
        x, y = X[:, 0], X[:, 1]
        x_pad = (x.max() - x.min() + 1e-9) * 0.07
        y_pad = (y.max() - y.min() + 1e-9) * 0.07
        x_min, x_max = float(x.min() - x_pad), float(x.max() + x_pad)
        y_min, y_max = float(y.min() - y_pad), float(y.max() + y_pad)

        xx, yy = np.meshgrid(
            np.linspace(x_min, x_max, 300, dtype=np.float32),
            np.linspace(y_min, y_max, 300, dtype=np.float32)
        )
        grid = np.c_[xx.ravel(), yy.ravel()].astype(np.float32)

        with torch.no_grad():
            logits = model(torch.from_numpy(grid).to(device))

        # --- Infer class count & build surface(s) ---
        if logits.dim() == 1:
            P = torch.sigmoid(logits).cpu().numpy().reshape(xx.shape)  # (H, W)
            C_eff = 2
        elif logits.dim() == 2 and logits.shape[1] == 1:
            P = torch.sigmoid(logits[:, 0]).cpu().numpy().reshape(xx.shape)
            C_eff = 2
        elif logits.dim() == 2 and logits.shape[1] > 1:
            C_eff = logits.shape[1]
            P_all = torch.softmax(logits, dim=1).cpu().numpy() \
                        .reshape(xx.shape + (C_eff,))                   # (H, W, C)
            Pcls = P_all.argmax(axis=2)                                 # (H, W)
        else:
            P = torch.sigmoid(logits.view(-1)).cpu().numpy().reshape(xx.shape)
            C_eff = 2

        if C_eff == 2:
            # Probability surface + 0.5 contour
            plt.contourf(xx, yy, P, levels=np.linspace(0, 1, 21), alpha=0.30)
            plt.contour(xx, yy, P, levels=[0.5], linewidths=2, colors="red")
        else:
            cmap = ListedColormap(plt.cm.tab10.colors[:C_eff])
            plt.contourf(xx, yy, Pcls, levels=np.arange(-0.5, C_eff, 1),
                         alpha=0.25, cmap=cmap)

        plt.scatter(x, y, c=y_i, s=25, edgecolors="k")
        plt.xlabel(feature_names[0] if feature_names and len(feature_names) > 0 else "x1")
        plt.ylabel(feature_names[1] if feature_names and len(feature_names) > 1 else "x2")

    else:
        plt.text(0.1, 0.9, "High-D classification: add custom viz",
                 transform=plt.gca().transAxes)
        plt.axis([0, 1, 0, 1])

    tmp = out_path + ".tmp.png"
    plt.tight_layout()
    plt.savefig(tmp, dpi=110, bbox_inches="tight")
    try:
        import os
        if os.path.exists(out_path):
            os.remove(out_path)
        os.replace(tmp, out_path)
    except Exception:
        pass

# --- Win95-ish plotting -------------------------------------------------
# --- RETRO95 helpers -------------------------------------------------------
def _retro95_bevel(fig):
    # moldura 3D "afundada" tipo Win95
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

from matplotlib.colors import ListedColormap

def _retro95_palette():
    # 10 cores sólidas “Win95-ish”
    return ["#0044AA","#FF00AA","#00AA00","#C00000",
            "#E1A500","#6B4E16","#7A1FA2","#008B8B",
            "#000000","#666666"]


def _quantize_0_100(arr):
    arr = np.asarray(arr, dtype=float) * 100.0
    return np.clip(np.rint(arr), 0, 100)  # inteiros 0..100

def _win95_axes(ax):
    # fundo cinza claro e moldura “bevel”
    fig = ax.figure
    fig.patch.set_facecolor("#C0C0C0")
    ax.set_facecolor("#E5E5E5")
    for spine in ax.spines.values():
        spine.set_visible(False)
    # moldura 3D fake
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
    ax  = fig.add_axes([0.04, 0.12, 0.94, 0.82])  # margens menores -> “preenche o quadrado”
    _retro95_axes(ax, xlim=(0,100), ylim=(0,100))

    # área ciano em “degraus” e linhas sem antialias
    ax.fill_between(xx, 0, yy, step="post",
                    facecolor="#00FFFF", edgecolor="#006B6B",
                    linewidth=1.2, antialiased=False)
    ax.plot(xx, yy, drawstyle="steps-post",
            color="#006B6B", linewidth=1.4, antialiased=False)

    ax.set_xlabel("") ; ax.set_ylabel("")
    ax.set_title(f"Fitting — epoch {epoch}/{epochs}  |  {metric_label}", fontsize=12)
    plt.tight_layout(pad=0.6)

    tmp = out_path + ".tmp.png"
    plt.savefig(tmp, dpi=110, bbox_inches="tight")
    plt.close(fig)
    os.replace(tmp, out_path)

def _retro95_datafit(ax, X, y_info, model, is_clf, feat_names, classes=None, proj="pca2"):
    _retro95_axes(ax)
    X = np.asarray(X, dtype=np.float32)

    # --- 2D view to plot (project if needed) ---
    if X.shape[1] > 2:
        x0, x1 = project_2d(X, method=("pca2" if proj == "pca2" else ("tsne2" if proj == "tsne2" else "none")))
        XY = np.c_[x0, x1].astype(np.float32)
        xlabel, ylabel = ("PC1", "PC2") if proj != "none" else ("x1", "x2")
    elif X.shape[1] == 2:
        XY = X
        xlabel = feat_names[0] if feat_names and len(feat_names) > 0 else "x1"
        ylabel = feat_names[1] if feat_names and len(feat_names) > 1 else "x2"
    else:
        XY = X  # 1D, handled below
        xlabel = feat_names[0] if feat_names else "x"
        ylabel = ""

    # --- palette/legend bits ---
    if is_clf:
        y_idx = np.asarray(y_info, dtype=np.int64)
        from matplotlib.colors import ListedColormap
        pal = _retro95_palette()
        cmap = ListedColormap(pal[:max(2, int(y_idx.max()) + 1)])

    # ---------- 1D ----------
    if XY.shape[1] == 1:
        x = XY[:, 0]
        x_min, x_max = float(x.min()), float(x.max())
        xs = np.linspace(x_min, x_max, 400, dtype=np.float32).reshape(-1, 1)

        if is_clf:
            with torch.no_grad():
                logits = model(torch.from_numpy(xs).float())
            if logits.dim() == 2 and logits.shape[1] > 1:
                p = torch.softmax(logits, dim=1).max(dim=1).values.cpu().numpy()
            else:
                p = torch.sigmoid(logits.view(-1)).cpu().numpy()
            # background bands (blocky)
            ax.fill_between(xs[:, 0], -0.5, 0.5, where=(p >= 0.5),
                            step="post", color="#FFAACF", alpha=0.35, antialiased=False)
            ax.fill_between(xs[:, 0], -0.5, 0.5, where=(p < 0.5),
                            step="post", color="#99BBFF", alpha=0.35, antialiased=False)

            # slight, deterministic jitter so classes separate a pouco
            rng = np.random.RandomState(0)
            jitter = (rng.randn(len(x)) * 0.08)
            ax.scatter(x, jitter, c=y_idx, cmap=cmap, vmin=0, vmax=cmap.N-1,
                       s=26, marker="s", edgecolors="black", linewidths=0.6, alpha=0.95)
            ax.set_ylim(-0.9, 0.9)
        else:
            # regression 1D
            ax.scatter(x, np.zeros_like(x), s=26, c="#0044AA", edgecolors="black",
                       marker="s", alpha=0.9, linewidths=0.6)
            order = np.argsort(x)
            with torch.no_grad():
                yhat = model(torch.from_numpy(x[order].reshape(-1, 1)).float()).cpu().numpy().reshape(-1)
            ax.plot(x[order], yhat, color="#C00000", linewidth=2.2, antialiased=False)

        ax.set_xlabel(xlabel);  ax.set_ylabel(ylabel)
        return

    # ---------- 2D ----------
    x, y = XY[:, 0], XY[:, 1]
    if is_clf:
        ax.scatter(x, y, c=y_idx, cmap=cmap, vmin=0, vmax=cmap.N-1,
                   s=22, marker="s", edgecolors="black", linewidths=0.5, alpha=0.95)

        # decision regions (blocky) + 0.5 contour for binário
        xpad = (x.max() - x.min() + 1e-9) * 0.07
        ypad = (y.max() - y.min() + 1e-9) * 0.07
        xx, yy = np.meshgrid(
            np.linspace(float(x.min()-xpad), float(x.max()+xpad), 240, dtype=np.float32),
            np.linspace(float(y.min()-ypad), float(y.max()+ypad), 240, dtype=np.float32)
        )
        grid = np.c_[xx.ravel(), yy.ravel()].astype(np.float32)
        with torch.no_grad():
            logits = model(torch.from_numpy(grid).float())

        if logits.dim() == 2 and logits.shape[1] > 1:
            cls = torch.argmax(torch.softmax(logits, dim=1), dim=1).cpu().numpy().reshape(xx.shape)
            ax.pcolormesh(xx, yy, cls, cmap=cmap, alpha=0.20, shading="nearest")
        else:
            P = torch.sigmoid(logits.view(-1)).cpu().numpy().reshape(xx.shape)
            ax.contour(xx, yy, P, levels=[0.5], colors="#C00000", linewidths=2.0, antialiased=False)
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
    """
    Tenta os.replace com pequenos retries para contornar locks transitórios
    (p.ex., GTK lendo o PNG). Falha silenciosamente jogando fora o tmp no final.
    """
    last_err = None
    for _ in range(max(1, retries)):
        try:
            os.replace(tmp_path, out_path)
            return
        except PermissionError as e:
            last_err = e
            time.sleep(delay)
        except OSError as e:
            # alguns antivírus/indexadores podem devolver erros parecidos
            last_err = e
            time.sleep(delay)
    # última tentativa — se ainda falhar, apaga o tmp para não acumular lixo
    try:
        os.remove(tmp_path)
    except Exception:
        pass
    if last_err:
        raise last_err

def save_plot_combo_retro95(values_0_1, epoch, epochs, X, y_plot_info,
                            model, is_clf, feat_names, out_path, metric_label,
                            classes=None, proj="pca2"):
    fig = plt.figure(figsize=(7.8, 5.6), dpi=110)  # a hair smaller
    ax_top = fig.add_axes([0.04, 0.61, 0.94, 0.33])
    ax_bot = fig.add_axes([0.04, 0.10, 0.94, 0.44])

    # topo: monitor cyan “blocky”
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

    # base: datafit retro (agora com projeção e cores por classe)
    _retro95_datafit(ax_bot, X, y_plot_info, model, is_clf, feat_names, classes=classes, proj=proj)

    plt.tight_layout(pad=0.2)

    tmp = out_path + ".tmp.png"

    # Nada de tight_layout aqui; ajustamos manualmente os eixos (add_axes)
    # e tiramos qualquer padding da figura:
    fig.subplots_adjust(left=0, right=1, top=1, bottom=0)
    plt.savefig(tmp, dpi=100, facecolor=fig.get_facecolor(),
                edgecolor=fig.get_facecolor(), bbox_inches=None, pad_inches=0)
    plt.close(fig)

    _safe_replace(tmp, out_path)  # seu helper com retry no Windows

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
                dp[i][j] = 1 + min(
                    dp[i - 1][j],    # deletion
                    dp[i][j - 1],    # insertion
                    dp[i - 1][j - 1] # substitution
                )
    return dp[n][m]

def lev_search(a: list, b: str):
    # tries to approximate b against all possible a
    full = {}
    for i in a:
        x = lev(i, b)
        full[i] = x
    return min(full, key=lambda k: full[k])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", required=True)
    ap.add_argument("--x", "--x-col", dest="x", required=True)   # one or more, comma separated
    ap.add_argument("--y", "--y-col", dest="y", required=True)
    ap.add_argument("--x-label", type=str, default=None)
    ap.add_argument("--y-label", type=str, default=None)
    # The model CHOOSES THE TASK.
    ap.add_argument("--model",
                    choices=["linreg","ridge","lasso","mlp_reg",   # regression
                             "logreg","mlp_cls"],                  # classification
                    default="linreg")
    ap.add_argument("--epochs", type=int, default=100)
    ap.add_argument("--train-pct", type=float, default=0.70)
    ap.add_argument("--proj", choices=["none","pca2","tsne2"], default="pca2")
    ap.add_argument("--color-by", type=str, default="residual")
    ap.add_argument("--frame-every", type=int, default=1)
    ap.add_argument("--out-plot", default="")
    ap.add_argument("--out-metrics", default="")
    # how often to render frames for classification/regression helpers
    ap.add_argument("--plot-every", type=int, default=1)
    # win95 plot ou moderno
    ap.add_argument("--plot-style", choices=["modern", "retro95"], 
                default=os.environ.get("AIFD_PLOT_STYLE", "modern"))

    args = ap.parse_args()

    df = pd.read_csv(args.csv)
    feat_names = [s.strip() for s in args.x.split(",") if s.strip()]
    y_feat = args.y

    df_cols = list(df.columns)

    fixed_feat_names = []
    for name in feat_names:
        if name not in df_cols:
            fixed_feat_names.append(lev_search(df_cols, name))
        else:
            fixed_feat_names.append(name)

    feat_names = fixed_feat_names
    if y_feat not in df_cols:
        y_feat = lev_search(df_cols, y_feat)

    X = df[feat_names].to_numpy()
    y = df[y_feat].to_numpy()

    # Decide task strictly from model (prevents wrong branch).
    is_clf = args.model in {"logreg","mlp_cls"}

    # Small, helpful debug line (shows up in GTK Logs)
    print(f"[dbg] task={'clf' if is_clf else 'reg'}  dim={X.shape[1]}  x='{args.x}'  y='{args.y}'", flush=True)

    # Split
    Xtr, Xte, ytr, yte = train_test_split(X, y, args.train_pct, seed=123)
    device = torch.device("cpu")
    in_dim = X.shape[1]

    # ---------------------- BUILD MODEL / TARGETS ----------------------
    if is_clf:
        # FIX: encode string/label targets -> indices
        ytr_idx, classes = encode_labels(ytr)
        yte_idx, _       = encode_labels(yte, classes)
        ncls = len(classes)

        if args.model == "mlp_cls":
            hidden = max(8, in_dim * 2)
            if ncls == 2:
                # binary MLP (1 logit)
                model = nn.Sequential(
                    nn.Linear(in_dim, hidden), nn.ReLU(),
                    nn.Linear(hidden, 1)
                ).to(device)
                loss_fn = nn.BCEWithLogitsLoss()
                yt = torch.from_numpy(ytr_idx.astype(np.float32)).view(-1, 1)
            else:
                # multi-class MLP (C logits)
                model = nn.Sequential(
                    nn.Linear(in_dim, hidden), nn.ReLU(),
                    nn.Linear(hidden, ncls)
                ).to(device)
                loss_fn = nn.CrossEntropyLoss()
                yt = torch.from_numpy(ytr_idx.astype(np.int64))
        else:  # "logreg"
            if ncls == 2:
                model = nn.Sequential(nn.Linear(in_dim, 1)).to(device)
                loss_fn = nn.BCEWithLogitsLoss()
                yt = torch.from_numpy(ytr_idx.astype(np.float32)).view(-1, 1)
            else:
                model = nn.Sequential(nn.Linear(in_dim, ncls)).to(device)
                loss_fn = nn.CrossEntropyLoss()
                yt = torch.from_numpy(ytr_idx.astype(np.int64))

        opt = optim.Adam(model.parameters(), lr=0.05)

    else:
        if args.model == "mlp_reg":
            model = MLPReg(in_dim).to(device)
        else:
            model = nn.Sequential(nn.Linear(in_dim, 1)).to(device)
        loss_fn = nn.MSELoss()
        weight_decay = 1e-2 if args.model == "ridge" else 0.0
        l1_lambda   = 1e-3 if args.model == "lasso" else 0.0
        opt = optim.Adam(model.parameters(), lr=0.05, weight_decay=weight_decay)
        yt = torch.from_numpy(ytr.astype(np.float32)).view(-1,1)

    Xt = torch.from_numpy(Xtr).float()

    plot_style = args.plot_style
    hist_vals = []         # métrica 0..1 por época
    metric_label = ""      # título “claro”


    # ----------------------------- TRAIN -------------------------------
    for epoch in range(1, args.epochs+1):
        opt.zero_grad()
        out = model(Xt)
        # CrossEntropy expects int64 class targets without view(-1,1)
        if is_clf and isinstance(loss_fn, nn.CrossEntropyLoss):
            loss = loss_fn(out, yt)
        else:
            loss = loss_fn(out, yt)

        if (not is_clf) and args.model == "lasso":
            l1 = sum(p.abs().sum() for p in model.parameters())
            loss = loss + 1e-3 * l1

        loss.backward(); opt.step()

        # --- métrica 0..1 para o gráfico retro ---
                # --- métrica por época (0..1) + label claro ---
        with torch.no_grad():
            if is_clf:
                logits_tr = model(Xt)
                if logits_tr.dim()==2 and logits_tr.shape[1]>1:
                    yhat_idx_tr = torch.argmax(logits_tr, dim=1).cpu().numpy()
                else:
                    yhat_idx_tr = (torch.sigmoid(logits_tr.view(-1))>=0.5).cpu().numpy().astype(int)
                if isinstance(loss_fn, nn.CrossEntropyLoss):
                    ytrue_idx_tr = yt.cpu().numpy()
                else:
                    ytrue_idx_tr = yt.view(-1).cpu().numpy().astype(int)
                acc_tr = float((yhat_idx_tr == ytrue_idx_tr).mean())
                hist_vals.append(acc_tr)
                metric_label = f"Training accuracy: {acc_tr*100:.1f}%"
            else:
                yhat_tr = model(Xt).cpu().numpy().reshape(-1)
                ytrue_tr = yt.view(-1).cpu().numpy().astype(float)
                # R² de treino (0..1)
                ss_res = float(np.sum((ytrue_tr - yhat_tr)**2))
                ss_tot = float(np.sum((ytrue_tr - ytrue_tr.mean())**2)) or 1.0
                r2 = max(0.0, min(1.0, 1.0 - ss_res/ss_tot))
                hist_vals.append(r2)
                metric_label = f"Training R²: {r2*100:.1f}%"

        # --- render frame (retro95 = combo; modern = comportamento anterior) ---
        if args.out_plot and (epoch % max(1, args.frame_every) == 0 or epoch == args.epochs):
            if plot_style == "retro95":
                save_plot_combo_retro95(
                    hist_vals, epoch, args.epochs,
                    Xtr,
                    (ytr_idx if is_clf else ytr),   # índices de classe no modo clf
                    model, is_clf, feat_names,
                    args.out_plot, metric_label,
                    classes=(classes if is_clf else None),
                    proj=args.proj                   # << usa o mesmo --proj do UI
                )

            else:
                if is_clf:
                    y_for_plot = ytr_idx
                    save_plot_classification(
                        Xtr, y_for_plot, model, epoch, args.epochs, args.out_plot,
                        feature_names=feat_names, device="cpu"
                    )
                else:
                    save_plot_regression(
                        Xtr, ytr.astype(float), model, epoch, args.epochs, args.out_plot,
                        x_label=(args.x_label or feat_names[0] if len(feat_names)==1 else "X"),
                        y_label=(args.y_label or args.y), proj=args.proj, color_by=args.color_by)


        print(f"epoch {epoch}/{args.epochs}  loss={loss.item():.6f}", flush=True)

    # ------------------------ TEST + METRICS ---------------------------
    model.eval()
    with torch.no_grad():
        if is_clf:
            logits_te = model(torch.from_numpy(Xte).float())
            if logits_te.dim() == 2 and logits_te.shape[1] > 1:
                # multi-class
                prob = torch.softmax(logits_te, dim=1).cpu().numpy()
                yhat_idx = np.argmax(prob, axis=1)
            else:
                # binary
                prob = torch.sigmoid(logits_te.view(-1)).cpu().numpy()
                yhat_idx = (prob >= 0.5).astype(int)
        else:
            pred = model(torch.from_numpy(Xte).float()).cpu().numpy().squeeze()

    if is_clf:
        # FIX: print detailed metrics + ASCII confusion (multi-class aware)
        from io import StringIO
        buf = StringIO()
        print_classification_report(yte_idx, yhat_idx, classes, stream=buf)
        text = buf.getvalue()
        print("\n" + text, flush=True)
    else:
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
