from flask import Flask, jsonify
import mysql.connector

app = Flask(__name__)

def get_connection():
    return mysql.connector.connect(
        host="localhost",        # ou IP do servidor
        user="root",
        password="",
        database="aifordummies"
    )

@app.route("/dados", methods=["GET"])
def get_dados():
    conn = get_connection()
    cursor = conn.cursor(dictionary=True)

    # pega o primeiro registro da tabela "usuarios"
    cursor.execute("SELECT nome, email FROM usuario;")
    row = cursor.fetchall()

    cursor.close()
    conn.close()

    if row:
        return jsonify(row)
    else:
        return jsonify({"nome": "—", "email": "—"})

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
