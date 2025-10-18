# python/connectors/auth.py
import os
import functools
from venv import logger
import jwt
import datetime
from flask import request, jsonify, g

# Secret / config
JWT_SECRET = os.getenv('JWT_SECRET', 'mudar_isto_para_uma_chave_forte_em_producao')
JWT_ALGORITHM = 'HS256'
JWT_EXP_SECONDS = int(os.getenv('JWT_EXP_SECONDS', 3600))  # 1 hora por default

# python/connectors/auth.py (apenas a função create_jwt_token)
# python/connectors/auth.py

def create_jwt_token(user_id: int, role: str = 'user', expires_in: int = None):
    if expires_in is None:
        expires_in = JWT_EXP_SECONDS
    now = datetime.datetime.now(datetime.timezone.utc)
    iat = int(now.timestamp())
    exp = int((now + datetime.timedelta(seconds=expires_in)).timestamp())

    # assegura sub como string (evita "Subject must be a string")
    payload = {
        'sub': str(int(user_id)),
        'role': role,
        'iat': iat,
        'exp': exp
    }
    token = jwt.encode(payload, JWT_SECRET, algorithm=JWT_ALGORITHM)
    if isinstance(token, bytes):
        token = token.decode('utf-8')
    return token

import logging

logger = logging.getLogger(__name__)
def decode_jwt_token(token: str):
    try:
        # pequeno leeway para clock skew
        payload = jwt.decode(token, JWT_SECRET, algorithms=[JWT_ALGORITHM], leeway=5)
        return payload
    except jwt.ExpiredSignatureError as e:
        logger.debug("decode_jwt_token: token expired: s", e)
        return {'_error': 'expired', 'reason': str(e)}
    except jwt.InvalidTokenError as e:
        # captura motivo específico emitido por PyJWT
        logger.debug("decode_jwt_token: invalid token: %s", e)
        return {'_error': 'invalid', 'reason': str(e)}
def get_bearer_token_from_request():
    auth = request.headers.get('Authorization', None)
    if auth:
        parts = auth.split()
        if len(parts) == 2 and parts[0].lower() == 'bearer':
            return parts[1].strip()
    t = request.cookies.get('access_token')
    if t:
        return t.strip()
    return None

def require_jwt(optional=False):
    def decorator(f):
        @functools.wraps(f)
        def wrapper(*args, **kwargs):
            token = get_bearer_token_from_request()
            logger.debug("require_jwt: Authorization token received: %s", token and ("(len=%d)" % len(token)) or "(none)")
            if not token:
                if optional:
                    g.user_id = None
                    g.user_role = None
                    return f(*args, **kwargs)
                return jsonify({'status':'ERROR','message':'Authorization required'}), 401

            # debug: inspeciona claims sem verificar assinatura (apenas para debug)
            try:
                noverify = jwt.decode(token, options={"verify_signature": False})
                logger.debug("require_jwt: token payload (no verify): %s", noverify)
            except Exception as e:
                logger.debug("require_jwt: failed to decode (no verify): %s", e)

            payload = decode_jwt_token(token)
            if isinstance(payload, dict) and payload.get('_error') == 'expired':
                return jsonify({'status':'ERROR','message':'Token expired','reason': payload.get('reason')}), 409
            if isinstance(payload, dict) and payload.get('_error') == 'invalid':
                return jsonify({'status':'ERROR','message':'Invalid token','reason': payload.get('reason')}), 410
            # ok
            g.user_id = int(payload.get('sub')) if payload.get('sub') is not None else None
            g.user_role = payload.get('role', 'user')
            return f(*args, **kwargs)
        return wrapper
    return decorator

