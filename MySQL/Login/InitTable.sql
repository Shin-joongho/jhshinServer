create database if not exists game_db

use game_db

create table if not exists T_Account
(
	AccountID	BIGINT unsigned primary key,
	ID			VARCHAR(16) not null,
	Passwords	VARBINARY(32) not null,
	Salt		VARBINARY(16) not null,
	createDate	DATETIME not null,
	last_Login	DATETIME
);
