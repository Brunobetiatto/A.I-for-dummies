import base64
import hashlib
import hmac
import traceback

def delete_user(cnx, user_id: int) -> bool:
    try:
        with cnx.cursor() as cursor:
            cursor.execute("DELETE FROM usuario WHERE idusuario = %s", (user_id,))
            affected = cursor.rowcount
        cnx.commit()
        return affected > 0
    except Exception:
        try:
            cnx.rollback()
        except Exception:
            pass
        raise


def delete_dataset(cnx, dataset_id: int) -> bool:
    try:
        with cnx.cursor() as cursor:
            cursor.execute("DELETE FROM dataset WHERE iddataset = %s", (dataset_id,))
            affected = cursor.rowcount
        cnx.commit()
        return affected > 0
    except Exception:
        try:
            cnx.rollback()
        except Exception:
            pass
        raise