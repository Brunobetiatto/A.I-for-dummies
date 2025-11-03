# database/CRUD/update.py
import base64
import os
import hashlib
import traceback
import pymysql

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

def update_user_info(cnx, user_id: int, updates: dict):
    """
    Atualiza campos do usuário. `updates` é um dict com chaves permitidas:
    nome, email, bio, avatar_url, role

    Retorna o dicionário do usuário atualizado (chave idusuario etc) usando get_user_by_id
    (não importe get_user_by_id aqui para evitar ciclos; quem chamar pode re-obter).
    """
    if not updates or not isinstance(updates, dict):
        return False

    allowed = ['nome', 'email', 'bio', 'avatar_url', 'role']
    set_clauses = []
    params = []
    for k in allowed:
        if k in updates:
            set_clauses.append(f"`{k}` = %s")
            params.append(updates[k])

    if not set_clauses:
        # nada a atualizar
        return True

    params.append(user_id)
    sql = "UPDATE usuario SET " + ", ".join(set_clauses) + " WHERE idusuario = %s"

    cur = cnx.cursor()
    try:
        cur.execute(sql, tuple(params))
        cnx.commit()
        return True
    except pymysql.IntegrityError as ie:
        # Por exemplo violação de unique email
        # Propaga a exceção ao chamador para tratamento
        raise
    except Exception as e:
        # Log e repassa
        traceback.print_exc()
        raise
    finally:
        cur.close()

def update_dataset_info(cnx, dataset_id: int, updates: dict) -> bool:
    """
    Atualiza campos do dataset. `updates` é um dict com chaves permitidas:
    nome, descricao, visibilidade

    Retorna True se atualizado com sucesso, False se nada foi atualizado.
    """
    if not updates or not isinstance(updates, dict):
        return False

    allowed = ['nome', 'descricao']
    set_clauses = []
    params = []
    for k in allowed:
        if k in updates:
            set_clauses.append(f"`{k}` = %s")
            params.append(updates[k])

    if not set_clauses:
        # nada a atualizar
        return False

    params.append(dataset_id)
    sql = "UPDATE dataset SET " + ", ".join(set_clauses) + " WHERE iddataset = %s"

    cur = cnx.cursor()
    try:
        cur.execute(sql, tuple(params))
        cnx.commit()
        return cur.rowcount > 0
    except Exception:
        traceback.print_exc()
        raise
    finally:
        cur.close()
