require('dotenv').config()
const express = require('express');
const db = require('./db');
const bycriptjs = require('bcryptjs');
const jwt = require('jsonwebtoken');

const SECRET_KEY = process.env.SECRET_KEY;
const app = express();
app.use(express.json());   //功能::解析 JSON 请求体到 req.body

//判断函数
function isValid(username, password) {
    if(!username || !password){return '用户名和密码不能为空';}
    if(username.length < 3 || username.length > 20){return '用户名长度必须在3-20之间';}
    if(password.length < 6 || password.length > 64){return '密码长度必须在6-64之间';}
    if(!username.match(/^[a-zA-Z0-9_]+$/)){return '用户名只能包含字母、数字和下划线';}
    return null;
}

//注册
app.post('/register', (req, res) => {
    const {username , password} = req.body;
    const error = isValid(username, password);
    if(error){
        return res.status(400).json({error});
    }

        // 校验参数完整性：缺任一则 400
     try{
         // bcrypt 单向哈希：不存明文，10 为计算复杂度（盐+哈希）
         const hash = bycriptjs.hashSync(password, 10);
         // 插入数据库,用?占位,返回插入的行数
         const insertSmst = db.prepare('INSERT INTO users (username, password) VALUES (?, ?)');
         insertSmst.run(username, hash);
         return res.status(201).json({message: '注册成功'});
     }
     catch (err){
         // 唯一键冲突：用户名重复 409
         if(err.code === 'SQLITE_CONSTRAINT_UNIQUE'){
             return res.status(409).json({error: '用户名已存在'});
         }
         return res.status(500).json({error: '服务器内部错误'});
     }
});
//登录
app.post('/login', (req, res) => {
   const {username, password} = req.body;
   const error = isValid(username, password);
   if(error) {return res.status(400).json({error});}

    // 查询用户；找不到或密码不匹配统一返回 401，防止枚举用户名
    const queryUser = db.prepare('SELECT * FROM users WHERE username = ?');
   const user = queryUser.get(username);
   if(!user || !bycriptjs.compareSync(password, user.password)){
       return res.status(401).json({error: '用户名或密码错误'});
   }
   const token = jwt.sign({username: username}, SECRET_KEY, {expiresIn: '1h'});
   return res.status(200).json({message: '登录成功',username,token});
});
// 获取用户信息
app.get('/me', (req, res) => {
    const authHeader = req.headers.authorization;
    if(!authHeader || !authHeader.startsWith('Bearer ')){
        return res.status(401).json({error: '未提供有效的授权信息'});
    }
    // 取出Bearer后面的token
    const token = authHeader.split(' ')[1];
    try{
        const decoded = jwt.verify(token, SECRET_KEY);
        return res.status(200).json({username: decoded.username});
    }
    catch (err){
        if(err.name === 'TokenExpiredError') {
            //TokenExpiredError
            return res.status(401).json({error: '授权已过期'});
        }
        return res.status(401).json({error: '无效的授权信息'});
    }
});

app.listen(3000, () => {
    console.log('\'Auth service listening on 3000');
})