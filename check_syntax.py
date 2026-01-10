#!/usr/bin/env python3
"""
简单的语法检查脚本
检查C代码文件是否有明显的语法错误
"""
import os
import re
import subprocess
import sys

def check_c_syntax(file_path):
    """检查C文件的语法"""
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # 简单的语法检查
        errors = []
        
        # 检查括号匹配
        bracket_count = 0
        brace_count = 0
        paren_count = 0
        
        for i, line in enumerate(content.split('\n'), 1):
            # 忽略注释
            if line.strip().startswith('//') or line.strip().startswith('/*'):
                continue
                
            bracket_count += line.count('[') - line.count(']')
            brace_count += line.count('{') - line.count('}')
            paren_count += line.count('(') - line.count(')')
            
            if bracket_count < 0 or brace_count < 0 or paren_count < 0:
                errors.append(f"Line {i}: 括号不匹配")
        
        if bracket_count != 0:
            errors.append(f"方括号不匹配: {bracket_count}")
        if brace_count != 0:
            errors.append(f"花括号不匹配: {brace_count}")
        if paren_count != 0:
            errors.append(f"圆括号不匹配: {paren_count}")
        
        # 检查常见的语法错误
        if re.search(r'#include\s*<[^>]*\.c>', content):
            errors.append("不应该包含.c文件")
        
        # 检查函数声明
        func_pattern = r'^\s*(?:static\s+)?(?:inline\s+)?(?:\w+\s+)+\*?\s*(\w+)\s*\([^)]*\)\s*(?:\{|;)'
        functions = re.findall(func_pattern, content, re.MULTILINE)
        
        return errors, functions
        
    except Exception as e:
        return [f"读取文件错误: {e}"], []

def check_project():
    """检查整个项目"""
    project_dir = os.path.dirname(os.path.abspath(__file__))
    errors = []
    all_functions = []
    
    # 检查所有C文件
    for root, dirs, files in os.walk(project_dir):
        # 跳过一些目录
        dirs[:] = [d for d in dirs if d not in ['.git', 'build', '__pycache__']]
        
        for file in files:
            if file.endswith('.c') and not file.startswith('check_syntax'):
                file_path = os.path.join(root, file)
                print(f"检查 {file_path}...")
                file_errors, functions = check_c_syntax(file_path)
                errors.extend([f"{file_path}: {error}" for error in file_errors])
                all_functions.extend([(file_path, func) for func in functions])
    
    # 输出结果
    print("\n" + "="*50)
    print("语法检查结果:")
    print("="*50)
    
    if errors:
        print("发现错误:")
        for error in errors:
            print(f"  - {error}")
    else:
        print("未发现语法错误")
    
    print(f"\n发现 {len(all_functions)} 个函数:")
    for file_path, func in all_functions:
        print(f"  - {os.path.basename(file_path)}: {func}")
    
    return len(errors) == 0

if __name__ == "__main__":
    success = check_project()
    sys.exit(0 if success else 1)