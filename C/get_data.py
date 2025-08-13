import mysql.connector

# Conecta no MySQL
conn = mysql.connector.connect(
    host="localhost",
    user="root",      # seu usuário MySQL
    password="",  # sua senha MySQL
    database="teste"
)
cursor = conn.cursor()
cursor.execute("SELECT valor FROM tabela")
dados = [valor for (valor,) in cursor.fetchall()]
conn.close()

# Imprime a quantidade e os valores
print(len(dados))
for valor in dados:
    print(valor)
