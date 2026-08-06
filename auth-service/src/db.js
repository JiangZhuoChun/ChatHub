// ==================== 模块：数据库依赖 ====================
const Datebase = require('better-sqlite3');

// ==================== 模块：数据库连接 ====================
// 功能：打开认证服务使用的 SQLite 数据库文件。
const db = new Datebase('auto.db');

// ==================== 模块：用户表初始化 ====================
// 功能：首次启动时创建 users 表，并保证用户名唯一、密码只保存哈希值。
db.exec('CREATE TABLE IF NOT EXISTS users (\n' +
    '    id INTEGER PRIMARY KEY AUTOINCREMENT,\n' +
    '    username TEXT NOT NULL UNIQUE,\n' +
    '    password TEXT NOT NULL,\n' +
    '    created_at TEXT DEFAULT (datetime(\'now\'))\n' +
    ');');

console.log('数据库初始化完毕');

// ==================== 模块：数据库对象导出 ====================
// 功能：向认证路由导出已初始化的数据库连接对象。
module.exports = db;
