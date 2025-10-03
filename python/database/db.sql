-- MySQL Workbench Forward Engineering

SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0;
SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0;
SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION';

-- -----------------------------------------------------
-- Schema mydb
-- -----------------------------------------------------
-- -----------------------------------------------------
-- Schema aifordummies
-- -----------------------------------------------------

-- -----------------------------------------------------
-- Schema aifordummies
-- -----------------------------------------------------
CREATE SCHEMA IF NOT EXISTS `aifordummies` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci ;
USE `aifordummies` ;

-- -----------------------------------------------------
-- Table `aifordummies`.`usuario`
-- -----------------------------------------------------
CREATE TABLE IF NOT EXISTS `aifordummies`.`usuario` (
  `idusuario` INT NOT NULL AUTO_INCREMENT,
  `nome` VARCHAR(50) NOT NULL,
  `email` VARCHAR(100) NOT NULL,
  `senha` VARCHAR(200) NOT NULL,
  `salt` VARCHAR(200) NOT NULL,
  `dataCadastro` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `bio` TEXT NULL DEFAULT NULL,
  `avatar_url` VARCHAR(2083) NULL DEFAULT NULL,
  `role` VARCHAR(50) NULL DEFAULT 'user',
  PRIMARY KEY (`idusuario`),
  UNIQUE INDEX `email` (`email` ASC) VISIBLE)
ENGINE = InnoDB
AUTO_INCREMENT = 19
DEFAULT CHARACTER SET = utf8mb4
COLLATE = utf8mb4_0900_ai_ci;


-- -----------------------------------------------------
-- Table `aifordummies`.`dataset`
-- -----------------------------------------------------
CREATE TABLE IF NOT EXISTS `aifordummies`.`dataset` (
  `iddataset` INT NOT NULL AUTO_INCREMENT,
  `usuario_idusuario` INT NOT NULL,
  `enviado_por_nome` VARCHAR(100) NULL DEFAULT NULL,
  `enviado_por_email` VARCHAR(100) NULL DEFAULT NULL,
  `nome` VARCHAR(100) NOT NULL,
  `descricao` VARCHAR(500) NULL DEFAULT NULL,
  `url` VARCHAR(2083) NOT NULL,
  `tamanho` VARCHAR(45) NULL DEFAULT NULL,
  `dataCadastro` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`iddataset`),
  UNIQUE INDEX `uq_dataset_usuario_nome` (`usuario_idusuario` ASC, `nome` ASC) VISIBLE,
  INDEX `idx_dataset_usuario` (`usuario_idusuario` ASC) VISIBLE,
  CONSTRAINT `fk_dataset_usuario`
    FOREIGN KEY (`usuario_idusuario`)
    REFERENCES `aifordummies`.`usuario` (`idusuario`)
    ON DELETE CASCADE)
ENGINE = InnoDB
AUTO_INCREMENT = 11
DEFAULT CHARACTER SET = utf8mb4
COLLATE = utf8mb4_0900_ai_ci;


-- -----------------------------------------------------
-- Table `aifordummies`.`modelo`
-- -----------------------------------------------------
CREATE TABLE IF NOT EXISTS `aifordummies`.`modelo` (
  `idmodelo` INT NOT NULL AUTO_INCREMENT,
  `usuario_idusuario` INT NOT NULL,
  `nome` VARCHAR(100) NOT NULL,
  `descricao` VARCHAR(200) NULL DEFAULT NULL,
  `tipo` VARCHAR(45) NOT NULL,
  `url` VARCHAR(2000) NOT NULL,
  `dataCriacao` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`idmodelo`),
  UNIQUE INDEX `uq_modelo_usuario_nome` (`usuario_idusuario` ASC, `nome` ASC) VISIBLE,
  CONSTRAINT `fk_modelo_usuario`
    FOREIGN KEY (`usuario_idusuario`)
    REFERENCES `aifordummies`.`usuario` (`idusuario`)
    ON DELETE CASCADE)
ENGINE = InnoDB
DEFAULT CHARACTER SET = utf8mb4
COLLATE = utf8mb4_0900_ai_ci;


-- -----------------------------------------------------
-- Table `aifordummies`.`password_reset_codes`
-- -----------------------------------------------------
CREATE TABLE IF NOT EXISTS `aifordummies`.`password_reset_codes` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `user_id` INT NOT NULL,
  `code` VARCHAR(6) NOT NULL,
  `reset_token` VARCHAR(128) NULL DEFAULT NULL,
  `expiration` DATETIME NOT NULL,
  `token_expiration` DATETIME NULL DEFAULT NULL,
  `used` TINYINT(1) NOT NULL DEFAULT '0',
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE INDEX `ux_reset_token` (`reset_token` ASC) VISIBLE,
  INDEX `idx_idusuario` (`user_id` ASC) VISIBLE,
  INDEX `idx_code` (`code` ASC) VISIBLE,
  INDEX `idx_expiration` (`expiration` ASC) VISIBLE,
  CONSTRAINT `fk_prc_usuario`
    FOREIGN KEY (`user_id`)
    REFERENCES `aifordummies`.`usuario` (`idusuario`)
    ON DELETE CASCADE)
ENGINE = InnoDB
AUTO_INCREMENT = 4
DEFAULT CHARACTER SET = utf8mb4
COLLATE = utf8mb4_unicode_ci;


-- -----------------------------------------------------
-- Table `aifordummies`.`treino`
-- -----------------------------------------------------
CREATE TABLE IF NOT EXISTS `aifordummies`.`treino` (
  `idmodelo` INT NOT NULL,
  `iddataset` INT NOT NULL,
  PRIMARY KEY (`idmodelo`, `iddataset`),
  INDEX `fk_mud_dataset` (`iddataset` ASC) VISIBLE,
  CONSTRAINT `fk_mud_dataset`
    FOREIGN KEY (`iddataset`)
    REFERENCES `aifordummies`.`dataset` (`iddataset`)
    ON DELETE RESTRICT,
  CONSTRAINT `fk_mud_modelo`
    FOREIGN KEY (`idmodelo`)
    REFERENCES `aifordummies`.`modelo` (`idmodelo`)
    ON DELETE CASCADE)
ENGINE = InnoDB
DEFAULT CHARACTER SET = utf8mb4
COLLATE = utf8mb4_0900_ai_ci;


SET SQL_MODE=@OLD_SQL_MODE;
SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS;
SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS;



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