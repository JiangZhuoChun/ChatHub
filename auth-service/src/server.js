// ==================== 模块：依赖与服务配置 ====================
require('dotenv').config();

const express = require('express');
const bcrypt = require('bcryptjs');
const jwt = require('jsonwebtoken');

const db = require('./db');
const SECRET_KEY = process.env.SECRET_KEY;
const app = express();

// ==================== 模块：请求正文解析 ====================
// 功能：将 JSON 请求正文解析到 req.body，供后续路由读取。
app.use(express.json());

// ==================== 模块：注册与登录输入校验 ====================
// 功能：校验用户名和密码的存在性、长度及用户名字符范围。
// 失败：参数不符合规则时返回中文错误信息；符合规则时返回 null。
function isValid(username, password) {
    if (!username || !password) {
        return '用户名和密码不能为空';
    }
    if (username.length < 3 || username.length > 20) {
        return '用户名长度必须在3-20之间';
    }
    if (password.length < 6 || password.length > 64) {
        return '密码长度必须在6-64之间';
    }
    if (!username.match(/^[a-zA-Z0-9_]+$/)) {
        return '用户名只能包含字母、数字和下划线';
    }
    return null;
}

// ==================== 模块：用户注册路由 ====================
// 功能：校验输入、哈希密码并向 users 表插入新用户。
// 失败：参数错误返回 400，用户名冲突返回 409，数据库异常返回 500。
app.post('/register',
    // 功能：处理一次注册 HTTP 请求并返回对应状态码和 JSON 结果。
    (req, res) => {
        const {username, password} = req.body;
        const error = isValid(username, password);
        if (error) {
            return res.status(400).json({error});
        }

        try {
            const hash = bcrypt.hashSync(password, 10);
            const insert_stmt = db.prepare('INSERT INTO users (username, password) VALUES (?, ?)');
            insert_stmt.run(username, hash);
            return res.status(201).json({message: '注册成功'});
        } catch (err) {
            if (err.code === 'SQLITE_CONSTRAINT_UNIQUE') {
                return res.status(409).json({error: '用户名已存在'});
            }
            return res.status(500).json({error: '服务器内部错误'});
        }
    });

// ==================== 模块：用户登录路由 ====================
// 功能：校验凭据，验证密码哈希后签发包含用户名的一小时 JWT。
// 失败：参数错误返回 400，用户名或密码不匹配返回 401。
app.post('/login',
    // 功能：处理一次登录 HTTP 请求并在验证成功后返回 token。
    (req, res) => {
        const {username, password} = req.body;
        const error = isValid(username, password);
        if (error) {
            return res.status(400).json({error});
        }

        const query_stmt = db.prepare('SELECT * FROM users WHERE username = ?');
        const user = query_stmt.get(username);
        if (!user || !bcrypt.compareSync(password, user.password)) {
            return res.status(401).json({error: '用户名或密码错误'});
        }

        const token = jwt.sign({username: username}, SECRET_KEY, {expiresIn: '1h'});
        return res.status(200).json({message: '登录成功', username, token});
    });

// ==================== 模块：当前用户查询路由 ====================
// 功能：验证 Authorization 中的令牌，并返回令牌中保存的用户名。
// 失败：缺少、过期或无效令牌时返回 401。
app.get('/me',
    // 功能：处理一次受保护的当前用户查询请求。
    (req, res) => {
        const auth_header = req.headers.authorization;
        if (!auth_header || !auth_header.startsWith('Bearer ')) {
            return res.status(401).json({error: '未提供有效的授权信息'});
        }

        const token = auth_header.split(' ')[1];
        try {
            const decoded = jwt.verify(token, SECRET_KEY);
            return res.status(200).json({username: decoded.username});
        } catch (err) {
            if (err.name === 'TokenExpiredError') {
                return res.status(401).json({error: '授权已过期'});
            }
            return res.status(401).json({error: '无效的授权信息'});
        }
    });

// ==================== 模块：认证服务启动 ====================
// 功能：监听本机 3000 端口，开始提供注册、登录和当前用户查询接口。
app.listen(3000,
    // 功能：端口成功绑定后输出服务启动日志。
    () => {
        console.log('Auth service listening on 3000');
    });
