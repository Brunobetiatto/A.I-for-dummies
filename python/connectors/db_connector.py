# db_connector.py
# conecta o banco de dados e recebe argumentos.
# os argumentos vão ser requisições feitas pelo
# main.c, que organiza a interface.

# por exemplo:
# ```shell
# python3 main.py -r "Dataset_5"  #lê os detalhes do Dataset_5 do banco de dados
# ```
# main.c --requisita--> db_connector.py --chama--> CRUD/read.py --retorna--> db_connector.py --retorna--> main.c

# import sys
# import mysql.connector

# def main(args) -> int:
#     # ??????
#     # !TEMP!
#     # ??????

#     # ? exemplo de implementação (WIP)
#     # MODE = args[0] # -r | -d | -u | -c / -a
#     # DB = args[1]
#     # TABLE = args[2] # por exemplo, pode ser "*" se quiser extrair todos os dados
#     # if len(args) > 4:
#     #     COLS  = args[3] # se houver apenas
    

#     # conn = mysql.connector.connect(
#     #     host="localhost",
#     #     user="root",      
#     #     password=PASS_API, 
#     #     database=DB
#     # )

#     # cursor = conn.cursor()
#     # if MODE == "-r": 
#     #     from database.CRUD.read import db_read
#     #     #...
#     # if MODE == "-d":
#     #     from database.CRUD.delete import db_delete
#     #     #...
#     # if MODE == "-u":
#     #     from database.CRUD.update import db_update
#     #     #...
#     # if MODE == "c" or MODE == "a":
#     #     from database.CRUD.update import db_create
#     #     #...


#     conn = mysql.connector.connect(
#         host="localhost",
#         user="root",      
#         password="pepsi@123", 
#         database="commtratta"
#     )

#     cursor = conn.cursor()
#     cursor.execute("SELECT * FROM usuarios")
#     dados = [v for v in cursor.fetchall()]
#     cols  = [desc[0] for desc in cursor.description]
#     conn.close()

#     print(", ".join(cols))

#     for v in dados:
#         print(", ".join(str(item) for item in v))
    
#     return 0

# if __name__ == "__main__":

#     main(sys.argv)

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import sys, io, argparse
import mysql.connector

# Force UTF-8 pipes on Windows
sys.stdin  = io.TextIOWrapper(sys.stdin.buffer,  encoding="utf-8", newline="\n")
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", newline="\n")

def open_conn(args):
    return mysql.connector.connect(
        host=args.host, port=args.port, user=args.user,
        password=args.password, database=args.db
    )

def list_tables(c):
    cur = c.cursor()
    cur.execute("SHOW TABLES")
    rows = [r[0] for r in cur.fetchall()]
    cur.close()
    sys.stdout.write("table\n")
    for t in rows:
        sys.stdout.write(f"{t}\n")

def dump_table(c, table):
    cur = c.cursor()
    cur.execute(f"SELECT * FROM `{table}`")
    cols = [d[0] for d in cur.description]
    sys.stdout.write(",".join(cols) + "\n")
    for row in cur.fetchall():
        sys.stdout.write(",".join("" if v is None else str(v) for v in row) + "\n")
    cur.close()

def describe_table(c, table):
    cur = c.cursor()
    cur.execute(f"DESCRIBE `{table}`")
    sys.stdout.write("Field,Type,Null,Key,Default,Extra\n")
    for r in cur.fetchall():
        sys.stdout.write(",".join("" if v is None else str(v) for v in r) + "\n")
    cur.close()

def serve(args):
    c = open_conn(args)
    sys.stdout.write("READY\n"); sys.stdout.flush()
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            if line.upper() == "QUIT":
                break
            elif line.upper() == "LIST":
                list_tables(c); sys.stdout.write("\x04\n"); sys.stdout.flush()
            elif line.upper().startswith("DUMP "):
                dump_table(c, line.split(" ", 1)[1]); sys.stdout.write("\x04\n"); sys.stdout.flush()
            elif line.upper().startswith("SCHEMA "):
                describe_table(c, line.split(" ", 1)[1]); sys.stdout.write("\x04\n"); sys.stdout.flush()
            else:
                sys.stdout.write("ERR unknown command\n\x04\n"); sys.stdout.flush()
        except Exception as e:
            sys.stdout.write(f"ERR {e}\n\x04\n"); sys.stdout.flush()
    c.close()

def oneshot(args):
    c = open_conn(args)
    if args.list_tables:
        list_tables(c)
    else:
        dump_table(c, args.table)
    c.close()

def parse_args(argv):
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="localhost")
    p.add_argument("--port", type=int, default=3306)
    p.add_argument("--user", required=True)
    p.add_argument("--password", required=True)
    p.add_argument("--db", required=True)
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--list-tables", action="store_true")
    g.add_argument("--table")
    g.add_argument("--serve", action="store_true")
    return p.parse_args(argv)

def main(argv):
    args = parse_args(argv[1:])
    if args.serve:
        serve(args)
    else:
        oneshot(args)
    return 0

if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

