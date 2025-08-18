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

-- Inserindo usuários de exemplo
INSERT INTO usuario (nome, email, senha, salt) VALUES
('Alice', 'alice@example.com', 'hashed_password_1', 'salt_1'),
('Bob', 'bob@example.com', 'hashed_password_2', 'salt_2'),
('Charlie', 'charlie@example.com', 'hashed_password_3', 'salt_3'),
('David', 'david@example.com', 'hashed_password_4', 'salt_4'),
('Eve', 'eve@example.com', 'hashed_password_5', 'salt_5'),
('Frank', 'frank@example.com', 'hashed_password_6', 'salt_6'),
('Grace', 'grace@example.com', 'hashed_password_7', 'salt_7'),
('Hank', 'hank@example.com', 'hashed_password_8', 'salt_8'),
('Ivy', 'ivy@example.com', 'hashed_password_9', 'salt_9'),
('Jack', 'jack@example.com', 'hashed_password_10', 'salt_10');

-- Inserindo datasets de exemplo
INSERT INTO dataset (usuario_idusuario, nome, descricao, url, tamanho) VALUES
(1, 'Dataset de Imagens', 'Conjunto de imagens para classificação', 'http://example.com/dataset1', '500MB'),
(1, 'Dataset de Texto', 'Conjunto de textos para análise de sentimentos', 'http://example.com/dataset2', '200MB'),
(2, 'Dataset de Áudio', 'Conjunto de áudios para reconhecimento de fala', 'http://example.com/dataset3', '1GB'),
(3, 'Dataset de Vídeo', 'Conjunto de vídeos para detecção de objetos', 'http://example.com/dataset4', '2GB'),
(4, 'Dataset de Dados Financeiros', 'Conjunto de dados para análise financeira', 'http://example.com/dataset5', '300MB'),
(5, 'Dataset de Saúde', 'Conjunto de dados médicos para diagnóstico', 'http://example.com/dataset6', '700MB'),
(6, 'Dataset de Clima', 'Conjunto de dados meteorológicos', 'http://example.com/dataset7', '1.5GB'),
(7, 'Dataset de Transporte', 'Conjunto de dados de tráfego e transporte', 'http://example.com/dataset8', '600MB'),
(8, 'Dataset de Redes Sociais', 'Conjunto de dados de interações em redes sociais', 'http://example.com/dataset9', '800MB'),
(9, 'Dataset de Jogos', 'Conjunto de dados de comportamento em jogos', 'http://example.com/dataset10', '1.2GB');