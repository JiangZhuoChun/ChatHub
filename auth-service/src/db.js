const Datebase = require('better-sqlite3');

const db = new Datebase('auto.db');
//建表
db.exec('CREATE TABLE IF NOT EXISTS users (\n' +
    '    id INTEGER PRIMARY KEY AUTOINCREMENT,\n' +   // 自增主键
    '    username TEXT NOT NULL UNIQUE,\n' +          // 用户名唯一，冲突抛 SQLITE_CONSTRAINT_UNIQUE
    '    password TEXT NOT NULL,\n' +            // bcrypt 哈希，绝不存明文
    '    created_at TEXT DEFAULT (datetime(\'now\'))\n' +
    ');');

console.log('数据库初始化完毕');
module.exports = db;