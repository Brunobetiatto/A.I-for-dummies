from __future__ import annotations

from typing import Tuple, List, Optional, Dict, Any, Union
from pathlib import Path

import json
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim

try:
    import pandas as pd
    from pandas import DataFrame, Series
except Exception:  # pandas optional
    DataFrame = Any  # type: ignore
    Series = Any     # type: ignore
    pd = None        # type: ignore

CACHE_PATH = Path("/cache")

Tensorable = Union[np.ndarray, List[float], List[int], "DataFrame", "Series"]

def _ensure_cache_dir(path: Path = CACHE_PATH) -> Path:
    """Ensure cache directory exists."""
    path.mkdir(parents=True, exist_ok=True)
    return path

def _to_numpy(x: Tensorable) -> np.ndarray:
    if pd is not None and isinstance(x, (pd.DataFrame, pd.Series)):
        return x.to_numpy()
    return np.asarray(x)

def _infer_task(y: np.ndarray, task: Optional[str] = None) -> str:
    """
    Guess task if not provided:
      - float dtype -> regression
      - int dtype -> classification
      - otherwise: if few uniques, classification; else regression
    """
    if task in {"classification", "regression"}:
        return task
    if np.issubdtype(y.dtype, np.floating):
        return "regression"
    if np.issubdtype(y.dtype, np.integer):
        return "classification"
    uniques = np.unique(y)
    return "classification" if len(uniques) <= max(20, int(np.sqrt(y.shape[0]) + 0.5)) else "regression"

def _r2_score(y_true: np.ndarray, y_pred: np.ndarray) -> float:
    y_true = y_true.astype(np.float64)
    y_pred = y_pred.astype(np.float64)
    ss_res = np.sum((y_true - y_pred) ** 2)
    ss_tot = np.sum((y_true - np.mean(y_true)) ** 2)
    return float(1.0 - ss_res / ss_tot) if ss_tot != 0 else 0.0

def _accuracy(y_true: np.ndarray, y_pred_labels: np.ndarray) -> float:
    y_true = y_true.reshape(-1)
    y_pred_labels = y_pred_labels.reshape(-1)
    return float((y_true == y_pred_labels).mean()) if y_true.size else 0.0

def _device_from_fit_params(fit_params: Optional[Dict[str, Any]]) -> torch.device:
    dev = (fit_params or {}).get("device", None)
    if isinstance(dev, torch.device):
        return dev
    if isinstance(dev, str):
        return torch.device(dev)
    return torch.device("cuda" if torch.cuda.is_available() else "cpu")

# -------------------- core API --------------------

def _train(
    dataX: Tensorable, dataY: Tensorable,
    testX: Tensorable, testY: Tensorable,
    trainee: nn.Module,
    fit_params: Optional[Dict[str, Any]] = None,
    **estimator_params: Any,  
) -> Tuple[nn.Module, float]:
    """
    Train a PyTorch model (nn.Module) on (dataX, dataY), evaluate on (testX, testY).

    fit_params keys (all optional):
      - task: "classification" | "regression" (auto if omitted)
      - epochs: int (default 50)
      - batch_size: int (default min(256, len(dataX)))
      - optimizer_cls: torch.optim.Optimizer subclass (default Adam)
      - optimizer_params: dict (e.g., {"lr": 1e-3})
      - loss_fn: torch loss instance or callable
                 (defaults: BCEWithLogitsLoss / CrossEntropyLoss / MSELoss)
      - device: "cuda" | "cpu" | torch.device (auto if omitted)
      - class_weights: 1D tensor/array for classification (optional)
    """
    fp = fit_params.copy() if fit_params else {}
    device = _device_from_fit_params(fp)

    Xtr = _to_numpy(dataX); ytr = _to_numpy(dataY)
    Xte = _to_numpy(testX); yte = _to_numpy(testY)
    if Xtr.ndim == 1: Xtr = Xtr.reshape(-1, 1)
    if Xte.ndim == 1: Xte = Xte.reshape(-1, 1)

    task = _infer_task(ytr, fp.get("task"))
    num_classes = None
    if task == "classification":

        ytr = ytr.astype(np.int64)
        yte = yte.astype(np.int64)
        classes = np.unique(ytr)
        num_classes = int(classes.size)
        if num_classes == 2:

            if set(classes.tolist()) != {0, 1}:
                mapping = {c: i for i, c in enumerate(sorted(classes))}
                ytr = np.vectorize(mapping.get)(ytr)
                yte = np.vectorize(mapping.get)(yte)
                classes = np.unique(ytr)

        num_classes = int(np.unique(ytr).size)

    Xtr_t = torch.from_numpy(Xtr).to(device=device, dtype=torch.float32)
    Xte_t = torch.from_numpy(Xte).to(device=device, dtype=torch.float32)

    if task == "regression":
        ytr_t = torch.from_numpy(ytr.astype(np.float32)).to(device=device).view(-1, 1)
        yte_np = yte.astype(np.float32)
    else:
        if num_classes == 2:
            ytr_t = torch.from_numpy(ytr.astype(np.float32)).to(device=device).view(-1, 1)  # BCE targets float
        else:
            ytr_t = torch.from_numpy(ytr.astype(np.int64)).to(device=device)                # CE targets long
        yte_np = yte.astype(np.int64)

    trainee = trainee.to(device)

    opt_cls = fp.get("optimizer_cls", optim.Adam)
    opt_params = fp.get("optimizer_params", {"lr": 1e-3})
    optimizer = opt_cls(trainee.parameters(), **opt_params)

    loss_fn = fp.get("loss_fn", None)
    if loss_fn is None:
        if task == "regression":
            loss_fn = nn.MSELoss()
        else:
            if num_classes == 2:

                w = fp.get("class_weights", None)
                if w is not None:
                    w = torch.as_tensor(np.asarray(w, dtype=np.float32), device=device)
                loss_fn = nn.BCEWithLogitsLoss(pos_weight=w) if w is not None else nn.BCEWithLogitsLoss()
            else:
                w = fp.get("class_weights", None)
                if w is not None:
                    w = torch.as_tensor(np.asarray(w, dtype=np.float32), device=device)
                loss_fn = nn.CrossEntropyLoss(weight=w)
    else:

        if isinstance(loss_fn, nn.Module):
            loss_fn = loss_fn.to(device)

    epochs = int(fp.get("epochs", 50))
    bs = int(fp.get("batch_size", min(256, max(1, Xtr_t.shape[0]))))
    shuffle = bool(fp.get("shuffle", True))

    if task == "regression":
        dataset = torch.utils.data.TensorDataset(Xtr_t, ytr_t)
    else:
        dataset = torch.utils.data.TensorDataset(Xtr_t, ytr_t)
    loader = torch.utils.data.DataLoader(dataset, batch_size=bs, shuffle=shuffle)

    trainee.train()
    for _ in range(epochs):
        for xb, yb in loader:
            optimizer.zero_grad(set_to_none=True)
            logits = trainee(xb)

            # shape fixes
            if task == "regression":
                if logits.ndim == 1: logits = logits.view(-1, 1)
                loss = loss_fn(logits, yb)
            else:
                if num_classes == 2:
                    # BCEWithLogits expects same shape; squeeze to (N,1) or (N,)
                    if logits.ndim == 1:
                        logits_b = logits
                    else:
                        # handle (N,1) or (N,) consistently
                        logits_b = logits.view(-1, 1)
                    loss = loss_fn(logits_b, yb)
                else:
                    # CrossEntropyLoss expects (N,C) logits and Long targets (N,)
                    if logits.ndim == 1:
                        logits = logits.view(-1, 1)
                    loss = loss_fn(logits, yb)

            loss.backward()
            optimizer.step()

    # ---- evaluation ----
    trainee.eval()
    with torch.no_grad():
        logits_te = trainee(Xte_t).detach().cpu().numpy()

    if task == "regression":
        pred = logits_te.squeeze()
        score = _r2_score(yte_np, pred)
    else:
        if num_classes == 2:
            # binary: logits -> sigmoid -> prob -> label
            if logits_te.ndim > 1 and logits_te.shape[1] == 1:
                logits_te = logits_te[:, 0]
            # threshold at 0.0 on logits equals 0.5 on prob
            y_pred = (logits_te >= 0.0).astype(np.int64)
        else:
            # multi-class: argmax over class dimension
            if logits_te.ndim == 1:
                y_pred = np.zeros_like(yte_np, dtype=np.int64)  # degenerate; avoid crash
            else:
                y_pred = logits_te.argmax(axis=1).astype(np.int64)
        score = _accuracy(yte_np, y_pred)

    return trainee, score


def _cache_model(
    model: nn.Module,
    model_name: Optional[str] = None,
    cache_path: Path = CACHE_PATH,
    extra_meta: Optional[Dict[str, Any]] = None,
) -> Path:
    """
    Save model weights (state_dict) + metadata to disk.
    Returns the .pt file path.

    Files written:
      - model.{name}.pt          (state_dict + meta embedded)
      - model.{name}.meta.json   (sidecar, human-readable; optional)
    """
    _ensure_cache_dir(cache_path)

    if model_name is None:
        model_name = model.__class__.__name__

    out_bin = cache_path / f"model.{model_name}.cache.pt"
    meta = {"model_name": model_name}
    if extra_meta:
        meta.update(extra_meta)

    payload = {
        "state_dict": model.state_dict(),
        "meta": meta,
        # NOTE: torch.save uses pickle; avoid saving the entire model class unless you control code at load time.
    }
    torch.save(payload, out_bin)

    # optional readable meta
    try:
        with open(cache_path / f"model.{model_name}.meta.json", "w", encoding="utf-8") as f:
            json.dump(meta, f, ensure_ascii=False, indent=2)
    except Exception:
        pass

    return out_bin


def _train_and_cache(
    dataX: Tensorable, dataY: Tensorable,
    testX: Tensorable, testY: Tensorable,
    trainee: nn.Module,
    model_name: Optional[str] = None,
    cache_path: Path = CACHE_PATH,
    fit_params: Optional[Dict[str, Any]] = None,
    **estimator_params: Any,  # kept for signature compat (ignored for torch)
) -> Tuple[nn.Module, float, Path]:
    """
    Train, evaluate, and persist the model. Returns (model, score, saved_path).

    Notes:
    - For classification, score = accuracy on test set.
    - For regression, score = R^2 on test set.
    - Pass training knobs via `fit_params` (epochs, batch_size, optimizer_cls, etc.).
    """
    # Preserve task + any meta you want written next to the weights
    fp = fit_params or {}
    task = fp.get("task", None)
    # Get shapes for meta
    Xnp = _to_numpy(dataX)
    if Xnp.ndim == 1: Xnp = Xnp.reshape(-1, 1)

    model, score = _train(
        dataX, dataY,
        testX, testY,
        trainee,
        fit_params=fit_params,
        **estimator_params
    )

    meta = {
        "task": task,
        "input_dim": int(Xnp.shape[1]),
        "score_name": "accuracy" if (task or _infer_task(_to_numpy(dataY))) == "classification" else "r2",
        "score_value": float(score),
    }
    saved_path = _cache_model(model, model_name=model_name, cache_path=cache_path, extra_meta=meta)
    return model, score, saved_path
