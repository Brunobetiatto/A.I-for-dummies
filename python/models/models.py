from __future__ import annotations

from typing import Tuple, List, Optional, Dict, Any
from pathlib import Path
from pandas import DataFrame
from sklearn.base import RegressorMixin, ClassifierMixin
import pickle as pkl

CACHE_PATH = Path("/cache")

def _ensure_cache_dir(path: Path = CACHE_PATH) -> Path:
    """
    Garante que o diretório de cache exista.
    """
    path.mkdir(parents=True, exist_ok=True)
    return path

def _train(
    dataX, dataY: DataFrame | List[float | int],
    testX, testY: DataFrame | List[float | int],
    trainee: RegressorMixin | ClassifierMixin,
    fit_params: Optional[Dict[str, Any]] = None,
    **estimator_params: Any,  # hiperparam
) -> Tuple[RegressorMixin | ClassifierMixin, float]:
    """
    Treina `trainee` em (dataX, dataY) e avalia em (testX, testY).

    - `estimator_params`: passam por `set_params(**...)` no estimador.
    - `fit_params`: vão para `fit(..., **fit_params)`, p.ex. sample_weight.
    """
    if estimator_params:
        trainee.set_params(**estimator_params)

    trainee.fit(dataX, dataY, **(fit_params or {}))
    score = trainee.score(testX, testY)
    return trainee, score


# salva o modelo na máquina local
def _cache_model(
    model: RegressorMixin | ClassifierMixin,
    model_name: Optional[str] = None,
    cache_path: Path = CACHE_PATH,
) -> Path:
    """
    Escreve as informações do modelo em um arquivo PKL que
    pode ser requisitado posteriormente para inferência.

    Retorna o caminho do arquivo salvo.
    """
    _ensure_cache_dir(cache_path)

    if model_name is None:
        model_name = model.__class__.__name__

    out_path = cache_path / f"model.{model_name}.cache.pkl"
    with open(out_path, "wb") as f:
        pkl.dump(obj=model, file=f)

    return out_path

# treina e salva o modelo
def _train_and_cache(
    dataX, dataY: DataFrame | List[float | int],
    testX, testY: DataFrame | List[float | int],
    trainee: RegressorMixin | ClassifierMixin,
    model_name: Optional[str] = None,
    cache_path: Path = CACHE_PATH,
    fit_params: Optional[Dict[str, Any]] = None,
    **estimator_params: Any,  
) -> Tuple[RegressorMixin | ClassifierMixin, float, Path]:
    """
    Treina, avalia e salva o modelo. Retorna (modelo, score, caminho_do_cache).
    """

    model, score = _train(
        dataX, dataY,
        testX, testY,
        trainee,
        fit_params=fit_params,
        **estimator_params
    )

    saved_path = _cache_model(model, model_name=model_name, cache_path=cache_path)
    
    return model, score, saved_path
