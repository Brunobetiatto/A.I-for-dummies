# database/CRUD/create.py
import os
import base64
import hashlib
import mysql.connector

PBKDF2_ROUNDS = 100_000

def _hash_password(password: str, salt: bytes) -> str:
    dk = hashlib.pbkdf2_hmac('sha256', password.encode('utf-8'), salt, PBKDF2_ROUNDS)
    return base64.b64encode(dk).decode('ascii')

def _gen_salt() -> bytes:
    return os.urandom(16)

def create_user(cnx: mysql.connector.MySQLConnection, nome: str, email: str, password: str):
    """
    Insere novo usuário na tabela `usuario`.
    Retorna dicionário com 'id', 'nome', 'email' em caso de sucesso.
    Lança mysql.connector.Error em caso de violação (ex.: email duplicado).
    """
    salt = _gen_salt()
    hash_b64 = _hash_password(password, salt)
    salt_b64 = base64.b64encode(salt).decode('ascii')

    cur = cnx.cursor()
    try:
        cur.execute(
            "INSERT INTO usuario (nome, email, senha, salt) VALUES (%s, %s, %s, %s)",
            (nome, email, hash_b64, salt_b64)
        )
        cnx.commit()
        uid = cur.lastrowid
        return {"id": uid, "nome": nome, "email": email}
    finally:
        cur.close()
