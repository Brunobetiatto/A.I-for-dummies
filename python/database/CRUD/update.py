# database/CRUD/update.py
import base64
import os
import hashlib

PBKDF2_ROUNDS = 100_000

def _hash_password(password: str, salt: bytes) -> str:
    dk = hashlib.pbkdf2_hmac('sha256', password.encode('utf-8'), salt, PBKDF2_ROUNDS)
    return base64.b64encode(dk).decode('ascii')

def _gen_salt() -> bytes:
    return os.urandom(16)

def reset_user_password(cnx, user_id: int, new_password: str):
    """
    Atualiza a senha e o salt de um usuário existente.
    """
    salt = _gen_salt()
    hash_b64 = _hash_password(new_password, salt)
    salt_b64 = base64.b64encode(salt).decode('ascii')

    cur = cnx.cursor()
    try:
        cur.execute(
            "UPDATE usuario SET senha = %s, salt = %s WHERE idusuario = %s",
            (hash_b64, salt_b64, user_id)
        )
        cnx.commit()
    finally:
        cur.close()
