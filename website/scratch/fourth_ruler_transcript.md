## 零、三把尺子的源码 + 第四把的全部运行

### `website/scratch/ruler_gui6.js`（第四把尺子，全文见该文件）

### 运行 1：基线
RULER=癸⁶(按数据流，不按形状)  SEED_PARAM=0  DENOM=362

### 运行 2：对照 (c) 空集 —— 种子表清空
RULER=癸⁶(按数据流，不按形状)  SEED_PARAM=0  DENOM=0

### 运行 3：对照 —— kill 表放回 B 组（`entries`/`keys`/`values`/`has`/`get`/`toString`）
RULER=癸⁶(按数据流，不按形状)  SEED_PARAM=0  DENOM=345

### 运行 4：对照 —— 把函数参数也算种子
RULER=癸⁶(按数据流，不按形状)  SEED_PARAM=1  DENOM=362

### 两处目标的逐字输出
```
$ node website/scratch/ruler_gui6.js . | grep render_check.js:661
website/scratch/render_check.js:661	
$ node website/scratch/ruler_gui6.js . | grep -c "check_citations.js:87\$"
0
```

### 十六路逐格（键 = 己⁶′/庚⁶/壬⁶/癸⁶）
```
0001 = 337   0010 = 11   0011 = 2
0100 = 21    0101 = 10   0110 = 21   0111 = 4
1100 = 22    1101 = 5    1110 = 18   1111 = 4
其余五格 = 0        合计 = 455
```
