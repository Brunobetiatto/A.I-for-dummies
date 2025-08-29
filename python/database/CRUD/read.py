# database/CRUD/read.py
import base64
import hashlib
import hmac
import traceback

PBKDF2_ROUNDS = 100_000

def get_user_by_email(cnx, email: str):
    """
    Retorna um dicionário com campos da tabela (idusuario, nome, email, senha, salt, dataCadastro)
    ou None se não existir.
    Suporta conexões pymysql com cursorclass=pymysql.cursors.DictCursor.
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

        # row é um dict porque a conexão usa DictCursor
        # Faz defensive checks em caso de colunas com nomes diferentes
        return {
            "id": row.get("idusuario") if isinstance(row, dict) else row[0],
            "nome": row.get("nome") if isinstance(row, dict) else row[1],
            "email": row.get("email") if isinstance(row, dict) else row[2],
            "senha": row.get("senha") if isinstance(row, dict) else row[3],
            "salt": row.get("salt") if isinstance(row, dict) else row[4],
            "dataCadastro": row.get("dataCadastro") if isinstance(row, dict) else row[5],
        }
    finally:
        cur.close()


def verify_login(cnx, email: str, password: str):
    """
    Verifica email+senha. Retorna o dicionário do usuário em caso de sucesso,
    ou None em caso de credenciais incorretas.
    """
    try:
        user = get_user_by_email(cnx, email)
    except Exception as e:
        print("Erro ao buscar usuário:", e)
        traceback.print_exc()
        return None

    if not user:
        # usuário inexistente
        return None

    try:
        # debug: inspeciona formato do user retornado (remova em produção)
        print("verify_login: user dict keys:", list(user.keys()))

        # salt deve ser string base64 no banco
        salt_b64 = user.get("salt")
        if not salt_b64:
            print("verify_login: salt ausente no registro do usuário")
            return None

        try:
            salt = base64.b64decode(salt_b64)
        except Exception as e:
            print("verify_login: falha ao decodificar salt (base64):", e)
            return None

        # deriva chave com PBKDF2-HMAC-SHA256
        dk = hashlib.pbkdf2_hmac('sha256', password.encode('utf-8'), salt, PBKDF2_ROUNDS)
        dk_b64 = base64.b64encode(dk).decode('ascii')

        stored_hash = user.get("senha")
        if not stored_hash:
            print("verify_login: senha armazenada ausente")
            return None

        # comparação segura
        if hmac.compare_digest(dk_b64, stored_hash):
            return {
                "id": user.get("id"),
                "nome": user.get("nome"),
                "email": user.get("email"),
                "dataCadastro": user.get("dataCadastro")
            }
        else:
            # senha incorreta
            return None

    except Exception as e:
        print("Error in verify_login:", e)
        traceback.print_exc()
        return None
