from flask import Flask, request, jsonify, g
import pymysql
import os, sys
import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
import secrets
import datetime
from datetime import timedelta
from urllib.parse import urlparse
import re
import uuid
from werkzeug.utils import secure_filename
from flask import send_from_directory, make_response
from db import DB_CONFIG, UPLOAD_FOLDER, DATASET_FOLDER
from auth import create_jwt_token, decode_jwt_token, require_jwt, JWT_EXP_SECONDS


# Adiciona o diretório pai ao sys.path para permitir imports absolutos como "database.*"
HERE = os.path.dirname(os.path.abspath(__file__))   # .../connectors
PARENT = os.path.abspath(os.path.join(HERE, ".."))  # .../python
if PARENT not in sys.path:
    sys.path.insert(0, PARENT)


from database.CRUD.create import create_user, create_dataset
from database.CRUD.read import verify_login
from database.CRUD.update import reset_user_password, update_user_info
from database.CRUD.read import get_user_by_id, get_datasets_by_user

app = Flask(__name__)

# --- configuração (no topo do arquivo, junto com app config) ---
UPLOAD_FOLDER = os.path.join(PARENT, 'uploads')  # caminho absoluto para a pasta uploads
os.makedirs(UPLOAD_FOLDER, exist_ok=True)
ALLOWED_EXTENSIONS = {'csv'}

def allowed_file(filename):
    return '.' in filename and filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS

@app.before_request
def log_auth_header():
    # apenas debug
    app.logger.debug("Authorization header: %s", request.headers.get('Authorization'))


# rota para servir os arquivos enviados (ajuste se estiver servindo static de outra forma)
@app.route('/uploads/<filename>', methods=['GET'])
def uploaded_file(filename):
    return send_from_directory(UPLOAD_FOLDER, filename, as_attachment=False)

ALLOWED_TABLES = ['usuario', 'dataset'] 

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
@require_jwt()
def dump_table(table_name):
    # validação simples do nome da tabela
    if not re.match(r'^[A-Za-z0-9_]+$', table_name):
        return jsonify({'status': 'ERROR', 'message': 'invalid table name'}), 400
    if ALLOWED_TABLES and table_name not in ALLOWED_TABLES:
        return jsonify({'status': 'ERROR', 'message': 'table not allowed'}), 403

    try:
        conn = get_db_connection()
        with conn.cursor() as cur:
            if table_name.lower() == 'dataset':
                # JOIN para trazer o nome do usuário que postou
                sql = """
                    SELECT d.*, u.nome AS usuario_nome, u.email AS usuario_email
                    FROM dataset d
                    LEFT JOIN usuario u ON d.usuario_idusuario = u.idusuario
                """
                cur.execute(sql)
            else:
                # consulta segura usando nome validado
                sql = f"SELECT * FROM `{table_name}`"
                cur.execute(sql)

            cols = [d[0] for d in cur.description]
            rows_dicts = cur.fetchall()  # espera lista de dicts (cursor dictionary=True)

        rows = []
        for r in rows_dicts:
            row = [r.get(c) for c in cols]
            rows.append(row)

        result = {'status': 'OK', 'columns': cols, 'data': rows}
        return jsonify(result)
    except Exception as e:
        app.logger.exception("dump_table error")
        return jsonify({'status': 'ERROR', 'message': str(e)}), 500

import traceback

@app.route('/login', methods=['POST'])
def login_route():
    try:
        data = request.get_json()
        if not data or 'email' not in data or 'password' not in data:
            return jsonify({'status': 'ERROR', 'message': 'Missing required fields'}), 400
        
        conn = get_db_connection()
        user = verify_login(conn, data['email'], data['password'])
        
        if user:
            # user retornado contem id, nome, email
            token = create_jwt_token(user_id=user['id'], role=user.get('role','user'))
            resp = {
                'status': 'OK',
                'user': user,
                'access_token': token,
                'expires_in': JWT_EXP_SECONDS
            }
            # opcional: set cookie seguro (se for uma app desktop, talvez não queira cookie)
            response = jsonify(resp)
            # response.set_cookie('access_token', token, httponly=True, max_age=JWT_EXP_SECONDS, samesite='Lax')
            return response
        else:
            return jsonify({'status': 'ERROR', 'message': 'Invalid credentials'}), 401
            
    except Exception as e:
        print("Login error:", str(e))
        traceback.print_exc()   # <--- mostra stack trace completo no console
        print("Request data:", data)
        return jsonify({'status': 'ERROR', 'message': 'Internal server error'}), 500
    


#--------------------------------email------------------------------------------------

# Configurações de email (substitua com suas credenciais)
MAIL_CONFIG = {
    'MAIL_SERVER': 'smtp.gmail.com',
    'MAIL_PORT': 587,
    'MAIL_USE_TLS': True,
    'MAIL_USERNAME': 'betiattobruno@gmail.com',
    'MAIL_PASSWORD': 'stqh iblc eehx fnyv'  # Use senha de aplicativo
}

# Rota para solicitar recuperação de senha
@app.route('/forgot-password', methods=['POST'])
def forgot_password():
    try:
        data = request.get_json()
        email = data.get('email')
        
        if not email:
            return jsonify({'status': 'ERROR', 'message': 'Email é obrigatório'}), 400
        
        conn = get_db_connection()
        with conn.cursor() as cur:
            # Verificar se o email existe
            cur.execute("SELECT idusuario, nome FROM usuario WHERE email = %s", (email))
            user = cur.fetchone()
            print("User found:", user)
            
            if not user:
                # Por segurança, não revelamos se o email existe ou não
                return jsonify({'status': 'OK', 'message': 'Se o email existir, um código de recuperação será enviado'})
            
            # Gerar código numérico de 6 dígitos
            reset_code = ''.join(secrets.choice('0123456789') for _ in range(6))
            expiration = datetime.datetime.now() + timedelta(minutes=15)  # Código válido por 15 minutos
            
            # Salvar código no banco de dados
            cur.execute(
                "INSERT INTO password_reset_codes (user_id, code, expiration, used) VALUES (%s, %s, NOW() + INTERVAL 15 MINUTE, FALSE)",
                (user['idusuario'], reset_code)
            )

            conn.commit()
        
        # Enviar email com o código
        send_reset_code_email(email, user['nome'], reset_code)
        
        return jsonify({'status': 'OK', 'message': 'Se o email existir, um código de recuperação será enviado'})
    
    except Exception as e:
        print("Error in forgot_password:", str(e))
        print("Request data:", data)
        return jsonify({'status': 'ERROR', 'message': 'Erro interno do servidorrrrrr'}), 500

# Rota para verificar o código de recuperação
@app.route('/verify-reset-code', methods=['POST'])
def verify_reset_code():
    try:
        data = request.get_json()
        email = data.get('email')
        code = data.get('code')
        print("Received code:", code)
        print("Received email:", email)
        
        if not email or not code:
            return jsonify({'status': 'ERROR', 'message': 'Email e código são obrigatórios'}), 400
        
        conn = get_db_connection()
        with conn.cursor() as cur:
            # Verificar se o código é válido e não expirou
            cur.execute("""
                SELECT prc.id 
                FROM password_reset_codes prc
                JOIN usuario u ON prc.user_id = u.idusuario
                WHERE u.email = %s AND prc.code = %s AND prc.expiration > NOW() AND prc.used = FALSE
            """, (email, code))
            code_data = cur.fetchone()
            
            if not code_data:
                return jsonify({'status': 'ERROR', 'message': 'Código inválido ou expirado'}), 400
            
            # Gerar token de redefinição 
            reset_token = secrets.token_urlsafe(32)
            token_expiration = datetime.datetime.now() + timedelta(minutes=15)
            
            # Atualizar o código com o token
            cur.execute(
                "UPDATE password_reset_codes SET reset_token = %s, token_expiration = %s WHERE id = %s",
                (reset_token, token_expiration, code_data['id'])
            )
            print("Reset token generated:", reset_token)
            conn.commit()
        
        return jsonify({'status': 'OK', 'reset_token': reset_token})
    
    except Exception as e:
        print("Error in verify_reset_code:", str(e))
        return jsonify({'status': 'ERROR', 'message': 'Erro interno do servidor'}), 500

# Rota para redefinir a senha com token válido
@app.route('/reset-password', methods=['POST'])
def reset_password():
    try:
        data = request.get_json()
        code = data.get('reset_token')  
        new_password = data.get('new_password')
        print("Reset password code:", code)
        print("New password:", new_password)
        
        if not code or not new_password:
            return jsonify({'status': 'ERROR', 'message': 'Código e nova senha são obrigatórios'}), 400
        
        conn = get_db_connection()
        with conn.cursor() as cur:
            # Verificar código válido e não expirado
            cur.execute("""
                SELECT user_id FROM password_reset_codes 
                WHERE reset_token = %s AND expiration > NOW() AND used = FALSE
            """, (code,))
            token_data = cur.fetchone()
            print("Token data fetched:", token_data)
            
            if not token_data:
                print("Token data:", code)
                return jsonify({'status': 'ERROR', 'message': 'Código inválido ou expirado'}), 400
            
            # Atualizar a senha
            reset_user_password(conn, token_data['user_id'], new_password)

            # Marcar código como usado
            cur.execute(
                "UPDATE password_reset_codes SET used = TRUE WHERE reset_token = %s",
                (code,)
            )

            conn.commit()
        
        return jsonify({'status': 'OK', 'message': 'Senha redefinida com sucesso'})
    
    except Exception as e:
        print("Error in reset_password:", str(e))
        return jsonify({'status': 'ERROR', 'message': 'Erro interno do servidor'}), 500


@app.route('/user/<int:user_id>', methods=['GET'])
def api_get_user(user_id):
    cnx = get_db_connection()
    try:
        user = get_user_by_id(cnx, user_id)
        if not user:
            return jsonify({"status": "ERR", "message": "User not found"}), 404
        # limpar campos sensíveis
        resp = {
            "id": user["idusuario"],
            "nome": user.get("nome"),
            "email": user.get("email"),
            "dataCadastro": user.get("dataCadastro").isoformat() if user.get("dataCadastro") else None,
            "bio": user.get("bio"),
            "avatar_url": user.get("avatar_url")
        }
        return jsonify({"status":"OK","user":resp})
    finally:
        cnx.close()


@app.route('/datasets/upload', methods=['POST'])
def datasets_upload():
    """
    Espera multipart/form-data:
      - file: o CSV a ser enviado
      - user_id: id do usuário que faz o upload (obrigatório)
      - enviado_por_nome: opcional (se ausente será buscado do usuário)
      - enviado_por_email: opcional (se ausente será buscado do usuário)
      - nome: opcional (se ausente usamos o nome do arquivo original)
      - descricao: opcional
    Retorna JSON com 'status':'OK' e 'dataset':{...} ou erro apropriado.
    """
    try:
        if 'file' not in request.files:
            return jsonify({'status': 'ERROR', 'message': 'No file part'}), 400
        file = request.files['file']

        nome_field = request.form.get('nome') or ''
        descricao = request.form.get('descricao') or ''
        enviado_por_nome = request.form.get('enviado_por_nome')
        enviado_por_email = request.form.get('enviado_por_email')

        form_user_id = request.form.get('user_id') or request.form.get('usuario_id')
        if not form_user_id:
            return jsonify({'status': 'ERROR', 'message': 'user_id is required'}), 400
        try:
            user_id_int = int(form_user_id)
        except ValueError:
            return jsonify({'status': 'ERROR','message':'user_id must be an integer'}), 400

        # Permitir upload se for o próprio usuário ou um admin
        if g.user_id != user_id_int and g.user_role != 'admin':
            return jsonify({'status':'ERROR','message':'Forbidden'}), 403

        # salvar arquivo com nome seguro + sufixo único
        orig_filename = secure_filename(file.filename)
        unique_suffix = uuid.uuid4().hex[:12]
        saved_filename = f"{os.path.splitext(orig_filename)[0]}_{unique_suffix}.csv"
        save_path = os.path.join(UPLOAD_FOLDER, saved_filename)
        file.save(save_path)
        file_size_bytes = os.path.getsize(save_path)
        tamanho_str = str(file_size_bytes)  # guarda como string (schema aceita VARCHAR)

        # construir URL pública (ajuste para produção)
        file_url = request.host_url.rstrip('/') + '/uploads/' + saved_filename

        # se enviado_por_* não foi informado, buscar no usuário
        cnx = get_db_connection()
        try:
            user = get_user_by_id(cnx, user_id_int)
            if not user:
                return jsonify({'status': 'ERROR', 'message': 'User not found'}), 404

            if not enviado_por_nome:
                enviado_por_nome = user.get('nome') or ''
            if not enviado_por_email:
                enviado_por_email = user.get('email') or ''
        finally:
            cnx.close()

        # nome do dataset: se não fornecido, usa nome original do arquivo (sem extensão)
        nome_to_store = nome_field or os.path.splitext(orig_filename)[0]

        # inserir no banco usando create_dataset (pode lançar IntegrityError em caso de unique constraint)
        cnx = get_db_connection()
        try:
            dataset = create_dataset(cnx,
                                     user_id_int,
                                     enviado_por_nome,
                                     enviado_por_email,
                                     nome_to_store,
                                     descricao,
                                     file_url,
                                     tamanho_str)
            return jsonify({'status': 'OK', 'dataset': dataset})
        except pymysql.err.IntegrityError as ie:
            # se violação de unique (usuario_idusuario, nome) — retorna 409
            return jsonify({'status': 'ERROR', 'message': 'Dataset with same name already exists for this user'}), 409
        except Exception as e:
            # cleanup: remover arquivo salvo em caso de erro de DB (opcional)
            try:
                if os.path.exists(save_path):
                    os.remove(save_path)
            except Exception:
                pass
            raise
        finally:
            cnx.close()

    except Exception as e:
        print("Error in datasets_upload:", str(e))
        return jsonify({'status': 'ERROR', 'message': str(e)}), 500

@app.route('/user/<int:user_id>/datasets', methods=['GET'])
def api_get_user_datasets(user_id):
    cnx = get_db_connection()
    try:
        datasets = get_datasets_by_user(cnx, user_id)
        # datasets: lista de dicts com campos iddataset, nome, descricao, url, tamanho, dataCadastro
        return jsonify({"status":"OK","datasets":datasets})
    finally:
        cnx.close()


AVATAR_FOLDER = os.path.join(UPLOAD_FOLDER, 'avatars')
os.makedirs(AVATAR_FOLDER, exist_ok=True)

DEFAULT_AVATAR_PATH = os.path.join(PARENT, 'assets', 'default_avatar.png')


@app.route('/user/<int:user_id>/avatar', methods=['GET'])
def api_get_user_avatar(user_id):
    cnx = None
    try:
        cnx = get_db_connection()
        user = get_user_by_id(cnx, user_id)
        if not user:
            return jsonify({'status': 'ERROR', 'message': 'User not found'}), 404

        # pega valor do campo avatar_url (compatível com dict ou objeto)
        avatar_val = None
        if isinstance(user, dict):
            avatar_val = user.get('avatar_url') or user.get('avatar') or None
        else:
            try:
                avatar_val = user[7]  # fallback para tuplas
            except Exception:
                avatar_val = None

        candidate_filename = None

        if avatar_val and isinstance(avatar_val, str) and avatar_val.strip():
            v = avatar_val.strip()
            try:
                parsed = urlparse(v)
                if parsed.scheme in ('http', 'https') and parsed.path:
                    candidate = os.path.basename(parsed.path)
                    if candidate:
                        candidate_filename = secure_filename(candidate)
                else:
                    # pode ser "uploads/avatars/file.png" ou apenas "file.png"
                    candidate = os.path.basename(v)
                    if candidate:
                        candidate_filename = secure_filename(candidate)
            except Exception:
                candidate = os.path.basename(v)
                candidate_filename = secure_filename(candidate) if candidate else None

        # função utilitária para aplicar cache header e retornar resposta
        def send_with_cache(directory, filename, max_age=3600):
            resp = send_from_directory(directory, filename, as_attachment=False)
            # garantir Response para poder modificar headers
            if not hasattr(resp, 'headers'):
                resp = make_response(resp)
            resp.headers['Cache-Control'] = f'public, max-age={int(max_age)}'
            return resp

        # 1) primeiro tenta AVATAR_FOLDER
        if candidate_filename:
            p1 = os.path.join(AVATAR_FOLDER, candidate_filename)
            if os.path.isfile(p1):
                return send_with_cache(AVATAR_FOLDER, candidate_filename)

            # 2) fallback para raiz de uploads
            p2 = os.path.join(UPLOAD_FOLDER, candidate_filename)
            if os.path.isfile(p2):
                return send_with_cache(UPLOAD_FOLDER, candidate_filename)

        # 3) se avatar_val aponta para um path absoluto dentro do diretório de uploads, sirva-o
        if avatar_val:
            try:
                abs_path = os.path.abspath(avatar_val)
                uploads_abs = os.path.abspath(UPLOAD_FOLDER)
                if abs_path.startswith(uploads_abs) and os.path.isfile(abs_path):
                    base_dir = os.path.dirname(abs_path)
                    fname = os.path.basename(abs_path)
                    return send_with_cache(base_dir, fname)
            except Exception:
                pass

        # 4) nada encontrado -> serve default avatar se existir
        if os.path.isfile(DEFAULT_AVATAR_PATH):
            assets_dir = os.path.dirname(DEFAULT_AVATAR_PATH)
            default_name = os.path.basename(DEFAULT_AVATAR_PATH)
            return send_with_cache(assets_dir, default_name)

        # 5) sem default -> 404 curto
        return jsonify({'status': 'ERROR', 'message': 'Avatar not found'}), 404

    except Exception as e:
        print("Error in api_get_user_avatar:", e)
        return jsonify({'status': 'ERROR', 'message': 'Internal server error'}), 500
    finally:
        if cnx:
            cnx.close()

ALLOWED_IMAGE_EXTENSIONS = {'png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp'}

def allowed_image(filename):
    return '.' in filename and filename.rsplit('.', 1)[1].lower() in ALLOWED_IMAGE_EXTENSIONS

# rota para servir avatares
@app.route('/avatars/<path:filename>', methods=['GET'])
def avatar_file(filename):
    try:
        return send_from_directory(AVATAR_FOLDER, filename, as_attachment=False)
    except Exception as e:
        app.logger.exception("Error serving avatar: %s", e)
        return jsonify({'status':'ERROR','message':'Avatar not found'}), 404

# rota para atualizar nome/bio/avatar (multipart/form-data)
@app.route('/user/<int:user_id>/avatar', methods=['POST'])
def api_update_user_avatar(user_id):
    try:
        # Pode receber multipart/form-data com: avatar (file), nome, bio
        nome = request.form.get('nome')
        bio = request.form.get('bio')

        avatar_file = request.files.get('avatar')

        avatar_url = None

        cnx = get_db_connection()
        try:
            # ensure user exists
            user = get_user_by_id(cnx, user_id)
            if not user:
                return jsonify({'status':'ERROR','message':'User not found'}), 404

            # if avatar provided, save file
            if avatar_file and avatar_file.filename:
                if not allowed_image(avatar_file.filename):
                    return jsonify({'status':'ERROR','message':'Invalid avatar file type'}), 400

                orig_filename = secure_filename(avatar_file.filename)
                unique_suffix = uuid.uuid4().hex[:12]
                saved_filename = f"{os.path.splitext(orig_filename)[0]}_{unique_suffix}{os.path.splitext(orig_filename)[1]}"
                save_path = os.path.join(AVATAR_FOLDER, saved_filename)
                avatar_file.save(save_path)

                # public URL
                avatar_url = request.host_url.rstrip('/') + '/avatars/' + saved_filename

            # build updates dict
            updates = {}
            if nome is not None:
                updates['nome'] = nome
            if bio is not None:
                updates['bio'] = bio
            if avatar_url is not None:
                updates['avatar_url'] = avatar_url

            if updates:
                # update_user_info imported from database.CRUD.update
                update_user_info(cnx, user_id, updates)

            # return updated user sanitized
            user_updated = get_user_by_id(cnx, user_id)
            if not user_updated:
                return jsonify({'status':'ERROR','message':'User not found after update'}), 500

            resp = {
                "id": user_updated.get("idusuario"),
                "nome": user_updated.get("nome"),
                "email": user_updated.get("email"),
                "dataCadastro": user_updated.get("dataCadastro").isoformat() if user_updated.get("dataCadastro") else None,
                "bio": user_updated.get("bio"),
                "avatar_url": user_updated.get("avatar_url"),
                "role": user_updated.get("role") if user_updated.get("role") else "user"
            }
            return jsonify({'status':'OK','user':resp})
        finally:
            cnx.close()
    except Exception as e:
        app.logger.exception("Error in api_update_user_avatar: %s", e)
        return jsonify({'status':'ERROR','message':str(e)}), 500

# Função para enviar email com o código
def send_reset_code_email(to_email, user_name, reset_code):
    msg = MIMEMultipart()
    msg['From'] = MAIL_CONFIG['MAIL_USERNAME']
    msg['To'] = to_email
    msg['Subject'] = 'Código de Recuperação de Senha - AIForDummies'
    
    body = f"""
    <h2>Recuperação de Senha</h2>
    <p>Olá {user_name},</p>
    <p>Você solicitou a recuperação de senha para sua conta no AIForDummies.</p>
    <p>Seu código de verificação é: <strong>{reset_code}</strong></p>
    <p>Este código expirará em 15 minutos.</p>
    <p>Se você não solicitou esta recuperação, ignore este email.</p>
    """
    
    msg.attach(MIMEText(body, 'html'))
    
    try:
        server = smtplib.SMTP(MAIL_CONFIG['MAIL_SERVER'], MAIL_CONFIG['MAIL_PORT'])
        server.starttls()
        server.login(MAIL_CONFIG['MAIL_USERNAME'], MAIL_CONFIG['MAIL_PASSWORD'])
        server.send_message(msg)
        server.quit()
    except Exception as e:
        print("Error sending email:", str(e))
        raise

# Função para fazer hash da senha (se ainda não existir)
def hash_password(password):
    import hashlib
    import os
    salt = os.urandom(32)
    key = hashlib.pbkdf2_hmac('sha256', password.encode('utf-8'), salt, 100000)
    return salt + key


if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5000)