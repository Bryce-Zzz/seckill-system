-- 秒杀系统数据库初始化脚本

-- 创建数据库
CREATE DATABASE IF NOT EXISTS seckill CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE seckill;

-- 商品表
DROP TABLE IF EXISTS products;
CREATE TABLE products (
    id VARCHAR(64) PRIMARY KEY COMMENT '商品ID',
    name VARCHAR(255) NOT NULL COMMENT '商品名称',
    stock INT NOT NULL DEFAULT 0 COMMENT '库存',
    price DECIMAL(10, 2) NOT NULL DEFAULT 0.00 COMMENT '价格',
    created_at BIGINT NOT NULL COMMENT '创建时间戳',
    updated_at BIGINT DEFAULT NULL COMMENT '更新时间戳',
    INDEX idx_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='商品表';

-- 订单表
DROP TABLE IF EXISTS orders;
CREATE TABLE orders (
    order_id VARCHAR(64) PRIMARY KEY COMMENT '订单ID',
    user_id VARCHAR(64) NOT NULL COMMENT '用户ID',
    product_id VARCHAR(64) NOT NULL COMMENT '商品ID',
    quantity INT NOT NULL DEFAULT 1 COMMENT '购买数量',
    status TINYINT NOT NULL DEFAULT 0 COMMENT '状态: 0=待处理, 1=已支付, 2=已取消',
    created_at BIGINT NOT NULL COMMENT '创建时间戳',
    updated_at BIGINT DEFAULT NULL COMMENT '更新时间戳',
    INDEX idx_user_id (user_id),
    INDEX idx_product_id (product_id),
    INDEX idx_created_at (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='订单表';

-- 秒杀活动表
DROP TABLE IF EXISTS seckill_activities;
CREATE TABLE seckill_activities (
    id VARCHAR(64) PRIMARY KEY COMMENT '活动ID',
    product_id VARCHAR(64) NOT NULL COMMENT '商品ID',
    seckill_price DECIMAL(10, 2) NOT NULL COMMENT '秒杀价',
    stock INT NOT NULL COMMENT '秒杀库存',
    start_time BIGINT NOT NULL COMMENT '开始时间',
    end_time BIGINT NOT NULL COMMENT '结束时间',
    status TINYINT NOT NULL DEFAULT 0 COMMENT '状态: 0=未开始, 1=进行中, 2=已结束',
    created_at BIGINT NOT NULL COMMENT '创建时间戳',
    INDEX idx_product_id (product_id),
    INDEX idx_status (status),
    INDEX idx_time (start_time, end_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='秒杀活动表';

-- 插入测试数据
INSERT INTO products (id, name, stock, price, created_at) VALUES
('PROD001', 'iPhone 15 Pro Max', 100, 9999.00, UNIX_TIMESTAMP(NOW()) * 1000),
('PROD002', 'MacBook Pro 14', 50, 15999.00, UNIX_TIMESTAMP(NOW()) * 1000),
('PROD003', 'AirPods Pro 2', 200, 1899.00, UNIX_TIMESTAMP(NOW()) * 1000),
('PROD004', 'Apple Watch Ultra 2', 150, 6499.00, UNIX_TIMESTAMP(NOW()) * 1000),
('PROD005', 'iPad Pro 12.9', 80, 9999.00, UNIX_TIMESTAMP(NOW()) * 1000);
