from flask import Flask, request, jsonify
import pymysql
import os, sys

# Adiciona o diretório pai ao sys.path para permitir imports absolutos como "database.*"
HERE = os.path.dirname(os.path.abspath(__file__))   # .../connectors
PARENT = os.path.abspath(os.path.join(HERE, ".."))  # .../python
if PARENT not in sys.path:
    sys.path.insert(0, PARENT)


from database.CRUD.create import create_user
from database.CRUD.read import verify_login

app = Flask(__name__)

# Configurações do banco de dados
DB_CONFIG = {
    'host': os.getenv('DB_HOST', 'hopper.proxy.rlwy.net'),
    'port': int(os.getenv('DB_PORT', 39703)),
    'user': os.getenv('DB_USER', 'root'),
    'password': os.getenv('DB_PASSWORD', 'hhzpIxzAuLBiDBPLELofDfZzDklgpVHD'),
    'database': os.getenv('DB_NAME', 'AIForDummies'),
    'autocommit': True,
    'cursorclass': pymysql.cursors.DictCursor
}

def get_db_connection():
    return pymysql.connect(**DB_CONFIG)

@app.route('/tables', methods=['GET'])
def list_tables():
    try:
        conn = get_db_connection()
        with conn.cursor() as cur:
            cur.execute("SHOW TABLES")
            tables = [r[0] for r in cur.fetchall()]
        return jsonify({'status': 'OK', 'tables': tables})
    except Exception as e:
        return jsonify({'status': 'ERROR', 'message': str(e)}), 500

@app.route('/table/<table_name>', methods=['GET'])
def dump_table(table_name):
    try:
        conn = get_db_connection()
        with conn.cursor() as cur:
            cur.execute(f"SELECT * FROM `{table_name}`")
            cols = [d[0] for d in cur.description]      # lista de nomes de coluna
            rows_dicts = cur.fetchall()                 # lista de dicts (cursor DictCursor)

        # converter rows (dicts) para arrays seguindo a ordem de cols
        rows = []
        for r in rows_dicts:
            row = []
            for c in cols:
                # extrai valor campo; None permanece None -> JSON null
                row.append(r.get(c))
            rows.append(row)

        result = {
            'status': 'OK',
            'columns': cols,
            'data': rows
        }
        return jsonify(result)
    except Exception as e:
        return jsonify({'status': 'ERROR', 'message': str(e)}), 500

@app.route('/schema/<table_name>', methods=['GET'])
def describe_table(table_name):
    try:
        conn = get_db_connection()
        with conn.cursor() as cur:
            cur.execute(f"DESCRIBE `{table_name}`")
            schema = cur.fetchall()
        return jsonify({'status': 'OK', 'schema': schema})
    except Exception as e:
        return jsonify({'status': 'ERROR', 'message': str(e)}), 500

@app.route('/user', methods=['POST'])
def create_user_route():
    try:
        data = request.get_json()
        if not data or 'nome' not in data or 'email' not in data or 'password' not in data:
            return jsonify({'status': 'ERROR', 'message': 'Missing required fields'}), 400
        
        conn = get_db_connection()
        result = create_user(conn, data['nome'], data['email'], data['password'])
        return jsonify({'status': 'OK', 'user': result})
    except Exception as e:
        return jsonify({'status': 'ERROR', 'message': str(e)}), 500

@app.route('/login', methods=['POST'])
def login_route():
    try:
        data = request.get_json()
        if not data or 'email' not in data or 'password' not in data:
            return jsonify({'status': 'ERROR', 'message': 'Missing required fields'}), 400
        
        conn = get_db_connection()
        user = verify_login(conn, data['email'], data['password'])
        if user:
            return jsonify({'status': 'OK', 'user': user})
        else:
            return jsonify({'status': 'ERROR', 'message': 'Invalid credentials'}), 401
    except Exception as e:
        return jsonify({'status': 'ERROR', 'message': str(e)}), 500

if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5000)