# database/CRUD/read.py
import base64
import hashlib
import hmac
import mysql.connector

PBKDF2_ROUNDS = 100_000

def get_user_by_email(cnx: mysql.connector.MySQLConnection, email: str):
    """
    Retorna um dicionário com campos da tabela (idusuario, nome, email, senha, salt, dataCadastro)
    ou None se não existir.
    """
    cur = cnx.cursor()
    try:
        cur.execute(
            "SELECT idusuario, nome, email, senha, salt, dataCadastro FROM usuario WHERE email = %s",
            (email,)
        )
        row = cur.fetchone()
        if not row:
            return None
        return {
            "id": row[0],
            "nome": row[1],
            "email": row[2],
            "senha": row[3],  # base64 string
            "salt": row[4],   # base64 string
            "dataCadastro": row[5],
        }
    finally:
        cur.close()

def verify_login(cnx: mysql.connector.MySQLConnection, email: str, password: str):
    """
    Verifica email+senha. Retorna o dicionário do usuário em caso de sucesso,
    ou None em caso de credenciais incorretas.
    """
    user = get_user_by_email(cnx, email)
    if not user:
        return None

    salt = base64.b64decode(user["salt"])
    dk = hashlib.pbkdf2_hmac('sha256', password.encode('utf-8'), salt, PBKDF2_ROUNDS)
    dk_b64 = base64.b64encode(dk).decode('ascii')

    # usar compare_digest para evitar timing attacks
    if hmac.compare_digest(dk_b64, user["senha"]):
        # não incluir senha/salt na resposta (se quiser, filtre)
        return {
            "id": user["id"],
            "nome": user["nome"],
            "email": user["email"],
            "dataCadastro": user["dataCadastro"]
        }
    return None
