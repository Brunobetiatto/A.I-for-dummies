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
            """
            INSERT INTO usuario (nome, email, senha, salt, dataCadastro, role)
            VALUES (%s, %s, %s, %s, NOW(), %s)
            """,
            (nome, email, hash_b64, salt_b64, 'user')
        )
        cnx.commit()
        uid = cur.lastrowid
        return {"id": uid, "nome": nome, "email": email}
    finally:
        cur.close()

def create_dataset(cnx, user_id: int,
                   enviado_por_nome: str,
                   enviado_por_email: str,
                   nome: str,
                   descricao: str,
                   url: str,
                   tamanho: str):
    """
    Insere um novo dataset na tabela `dataset` conforme schema fornecido.
    Retorna um dict com os campos do dataset inserido (incluindo iddataset).
    Pode lançar exceção (IntegrityError, etc) que o caller deve tratar.
    """
    cur = cnx.cursor()
    try:
        sql = """
            INSERT INTO dataset
              (usuario_idusuario, enviado_por_nome, enviado_por_email,
               nome, descricao, url, tamanho, dataCadastro)
            VALUES (%s, %s, %s, %s, %s, %s, %s, NOW())
        """
        cur.execute(sql, (user_id, enviado_por_nome, enviado_por_email,
                          nome, descricao, url, tamanho))
        try:
            cnx.commit()
        except Exception:
            pass

        dataset_id = cur.lastrowid
        return {
            "iddataset": dataset_id,
            "usuario_idusuario": user_id,
            "enviado_por_nome": enviado_por_nome,
            "enviado_por_email": enviado_por_email,
            "nome": nome,
            "descricao": descricao,
            "url": url,
            "tamanho": tamanho
        }
    finally:
        cur.close()


