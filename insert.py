# insert.py
import sys
import mysql.connector

def main(args):
    if len(args) < 3:
        print("Uso: python insert.py <id> <nome>")
        return 1

    try:
        id_val = int(args[1])
        nome_val = args[2]
    except ValueError:
        print("ID precisa ser um número inteiro!")
        return 1

    try:
        conn = mysql.connector.connect(
            host="localhost",
            user="root",      # seu usuário MySQL
            password="",      # sua senha
            database="teste"
        )
        cursor = conn.cursor()
        cursor.execute("INSERT INTO tabela (id, nome) VALUES (%s, %s)", (id_val, nome_val))
        conn.commit()
        print(f"Registro inserido: {id_val} | {nome_val}")
    except mysql.connector.Error as err:
        print("Erro ao inserir no banco:", err)
        return 1
    finally:
        if conn.is_connected():
            cursor.close()
            conn.close()
    
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
