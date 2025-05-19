#!/bin/bash
# 初始化LogCabin集群脚本

set -e

echo "等待所有LogCabin节点启动..."
sleep 10  # 给节点足够的启动时间

echo "正在初始化集群..."
# 定义节点列表
ALLSERVERS="logcabin-node1:5254,logcabin-node2:5254,logcabin-node3:5254"

# 使用Reconfigure工具配置集群成员
/usr/bin/LogCabin --cluster=$ALLSERVERS --reconfigure set logcabin-node1:5254 logcabin-node2:5254 logcabin-node3:5254

echo "初始化完成。集群已配置如下节点："
echo "- logcabin-node1:5254 (ID: 1)"
echo "- logcabin-node2:5254 (ID: 2)"
echo "- logcabin-node3:5254 (ID: 3)"

# 执行一个简单测试确认集群工作正常
echo "测试集群连接..."
echo "hello cluster" | /usr/bin/LogCabin --cluster=$ALLSERVERS --treeop write /test
/usr/bin/LogCabin --cluster=$ALLSERVERS --treeop read /test

echo "集群初始化和测试完成！"
