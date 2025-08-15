CREATE DATABASE `AIForDummies` CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
USE `AIForDummies`;

-- ============== TABELA: usuario ==============
CREATE TABLE usuario (
  idusuario      INT AUTO_INCREMENT PRIMARY KEY,
  nome	         VARCHAR(50) NOT NULL,
  email          VARCHAR(100) NOT NULL UNIQUE,
  senha          VARCHAR(200) NOT NULL,
  salt           VARCHAR(200) NOT NULL,
  dataCadastro   DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ============== TABELA: dataset (1 usuário -> N datasets) ==============
CREATE TABLE dataset (
  iddataset           INT AUTO_INCREMENT PRIMARY KEY,
  usuario_idusuario   INT NOT NULL,
  nome                VARCHAR(100) NOT NULL,
  descricao           VARCHAR(500),
  url                 VARCHAR(2083) NOT NULL,  
  tamanho             VARCHAR(45),             
  dataCadastro        DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_dataset_usuario
    FOREIGN KEY (usuario_idusuario) REFERENCES usuario(idusuario)
    ON DELETE CASCADE,
  CONSTRAINT uq_dataset_usuario_nome
    UNIQUE (usuario_idusuario, nome)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ============== TABELA: modelo (1 usuário -> N modelos) ==============
CREATE TABLE modelo (
  idmodelo            INT AUTO_INCREMENT PRIMARY KEY,
  usuario_idusuario   INT NOT NULL,
  nome                VARCHAR(100) NOT NULL,
  descricao           VARCHAR(200),
  tipo                VARCHAR(45) NOT NULL,
  url                 VARCHAR(2000) NOT NULL,  -- link dos pesos/artefato
  dataCriacao         DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_modelo_usuario
    FOREIGN KEY (usuario_idusuario) REFERENCES usuario(idusuario)
    ON DELETE CASCADE,
  CONSTRAINT uq_modelo_usuario_nome
    UNIQUE (usuario_idusuario, nome)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ============== TABELA DE LIGAÇÃO N↔N: modelo treina com 1 ou mais datasets e datasets servem para treinar 1 ou mais modelos ==============
CREATE TABLE treino (
  idmodelo   INT NOT NULL,
  iddataset  INT NOT NULL,
  PRIMARY KEY (idmodelo, iddataset),
  CONSTRAINT fk_mud_modelo
    FOREIGN KEY (idmodelo)  REFERENCES modelo(idmodelo)   ON DELETE CASCADE,
  CONSTRAINT fk_mud_dataset
    FOREIGN KEY (iddataset) REFERENCES dataset(iddataset) ON DELETE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;