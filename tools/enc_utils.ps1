# =====================================================================
# 标准中文文件读写工具 (GBK/UTF-8) - 一劳永逸,禁止再用 Node 拼接
# 用法:
#   . D:\inimerse_stable\tools\enc_utils.ps1
#   ReadGbk  "path"        -> 返回字符串
#   WriteGbk "path" str    -> 写 GBK
#   ReadUtf8 "path"        -> 返回字符串
#   WriteUtf8 "path" str   -> 写 UTF-8
#   AppendGbk "path" str   -> 追加 GBK
# 规则: 中文文件一律 GBK; 新增内容先用 UTF-8 源文件写, 再转 GBK 写入
# =====================================================================
$script:GBK  = [System.Text.Encoding]::GetEncoding(936)
$script:UTF8 = [System.Text.Encoding]::UTF8

function ReadGbk([string]$p) { return [System.IO.File]::ReadAllText($p, $script:GBK) }
function WriteGbk([string]$p, [string]$s) { [System.IO.File]::WriteAllText($p, $s, $script:GBK) }
function AppendGbk([string]$p, [string]$s) {
  $sw = [System.IO.StreamWriter]::new($p, $true, $script:GBK); $sw.Write($s); $sw.Close()
}
function ReadUtf8([string]$p) { return [System.IO.File]::ReadAllText($p, $script:UTF8) }
function WriteUtf8([string]$p, [string]$s) { [System.IO.File]::WriteAllText($p, $s, $script:UTF8) }

# 校验: 返回无效 GBK 字节数(0 = 完全干净)
function Test-GbkClean([string]$p) {
  $b = [System.IO.File]::ReadAllBytes($p)
  $bad = 0; $i = 0
  while ($i -lt $b.Length) {
    $c = $b[$i]
    if ($c -ge 0x81 -and $c -le 0xFE) {
      if ($i + 1 -ge $b.Length) { $bad++; $i++ }
      else { $t = $b[$i+1]; if ($t -lt 0x40 -or $t -eq 0x7f) { $bad++ }; $i += 2 }
    } elseif ($c -ge 0x80) { $bad++; $i++ }
    else { $i++ }
  }
  return $bad
}

# 校验: 返回 GBK 解码替换字符数(0 = 完全干净)
function Test-GbkText([string]$p) {
  $s = ReadGbk $p
  return ([regex]::Matches($s, [char]0xFFFD)).Count
}
