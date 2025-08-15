import sys

import mysql.connector

def main(args) -> int:
    conn = mysql.connector.connect(
        host="localhost",
        user="root",      # seu usuário MySQL
        password="",  # sua senha MySQL
        database="teste"
    )

    cursor = conn.cursor()
    cursor.execute("SELECT * FROM tabela")
    dados = [(id, nome) for (id, nome) in cursor.fetchall()]
    conn.close()

    print("ID | NOME")
    print("---+------")
    for (id, nome) in dados:
        print(id, " |", nome, flush=True)
    
    return 0

if __name__ == "__main__":
    main(sys.argv)