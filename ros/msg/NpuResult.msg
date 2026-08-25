# NPU 推理结果消息
int32 class_id          # 分类结果索引
string label            # 分类标签（normal/warning/critical）
int32[] scores          # 各类分数
int32[] input           # 输入特征
