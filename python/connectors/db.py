# config.py
import os
import pymysql.cursors
from dotenv import load_dotenv

# Carrega variáveis do .env se existir
load_dotenv()

# Configurações do banco de dados
DB_CONFIG = {
    'host': os.getenv('DB_HOST', 'localhost'),
    'port': int(os.getenv('DB_PORT', 3306)),
    'user': os.getenv('DB_USER', 'root'),
    'password': os.getenv('DB_PASSWORD', ''),
    'database': os.getenv('DB_NAME', 'aifordummies'),
    'autocommit': True,
    'cursorclass': pymysql.cursors.DictCursor
}

# Diretórios e uploads
BASE_DIR = os.path.abspath(os.path.dirname(__file__))
UPLOAD_FOLDER = os.path.join(BASE_DIR, 'uploads')
DATASET_FOLDER = os.path.join(BASE_DIR, 'datasets')

# Outras configurações opcionais
MAX_CONTENT_LENGTH = 200 * 1024 * 1024  # 200MB
SECRET_KEY = os.getenv('SECRET_KEY', 'supersecret')
