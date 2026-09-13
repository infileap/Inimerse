#!/usr/bin/env python3
"""Translate Eidos aliases to the current record core syntax."""
import argparse, re

WORD = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')

def _matching_brace(src, opening):
    depth = 0
    i = opening
    quote = None
    line_comment = False
    block_comment = False
    while i < len(src):
        c = src[i]
        n = src[i + 1] if i + 1 < len(src) else ''
        if block_comment:
            if c == ']' and n == '#':
                block_comment = False
                i += 2
                continue
            i += 1
            continue
        if line_comment:
            if c in '\r\n':
                line_comment = False
            i += 1
            continue
        if quote:
            if c == '\\':
                i += 2
                continue
            if c == quote:
                quote = None
            i += 1
            continue
        if c in ('"', "'"):
            quote = c
            i += 1
            continue
        if c == '#' and n == '[':
            block_comment = True
            i += 2
            continue
        if c == '#':
            line_comment = True
            i += 1
            continue
        if c == '/' and n == '/':
            line_comment = True
            i += 2
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError('unclosed Eidos body')

def _matching_pair(src, opening, left='(', right=')'):
    depth = 0
    i = opening
    quote = None
    while i < len(src):
        c = src[i]
        if quote:
            if c == '\\':
                i += 2
                continue
            if c == quote:
                quote = None
            i += 1
            continue
        if c in ('"', "'"):
            quote = c
        elif c == left:
            depth += 1
        elif c == right:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError('unclosed Eidos parameter list')

def _line_end(src, start):
    depth = 0
    i = start
    quote = None
    while i < len(src):
        c = src[i]
        if quote:
            if c == '\\':
                i += 2
                continue
            if c == quote:
                quote = None
            i += 1
            continue
        if c in ('"', "'"):
            quote = c
        elif c in '([{':
            depth += 1
        elif c in ')]}':
            if depth > 0:
                depth -= 1
        elif c in '\r\n' and depth == 0:
            return i
        i += 1
    return i

def _skip_space_comments(src, i):
    while i < len(src):
        while i < len(src) and src[i].isspace():
            i += 1
        if src.startswith('#[', i):
            end = src.find(']#', i + 2)
            if end < 0:
                raise ValueError('unclosed Eidos block comment')
            i = end + 2
            continue
        if src.startswith('#', i) or src.startswith('//', i):
            end = _line_end(src, i)
            i = end
            continue
        break
    return i

def _parse_eidos_members(body):
    members = []
    i = 0
    while True:
        i = _skip_space_comments(body, i)
        if i >= len(body):
            return members
        match = WORD.match(body, i)
        if not match:
            raise ValueError('Eidos members must start with an identifier')
        name = match.group(0)
        i = _skip_space_comments(body, match.end())
        if i < len(body) and body[i] == '=':
            end = _line_end(body, i + 1)
            expr = body[i + 1:end].strip()
            if not expr:
                raise ValueError(f'Eidos field {name} needs a default expression')
            members.append(('field', name, expr))
            i = end
            continue
        params = None
        if i < len(body) and body[i] == '(':
            end = _matching_pair(body, i)
            params = [p.strip() for p in body[i + 1:end].split(',') if p.strip()]
            if any(not WORD.fullmatch(p) for p in params):
                raise ValueError(f'Eidos method {name} has invalid parameters')
            i = _skip_space_comments(body, end + 1)
        if i < len(body) and body[i] == '{':
            end = _matching_brace(body, i)
            method_body = body[i + 1:end]
            members.append(('method', name, params or [], method_body, None))
            i = end + 1
            continue
        if i + 1 < len(body) and body[i:i + 2] == '->':
            end = _line_end(body, i + 2)
            expr = body[i + 2:end].strip()
            if not expr:
                raise ValueError(f'Eidos method {name} needs an expression')
            members.append(('method', name, params or [], expr, True))
            i = end
            continue
        raise ValueError(f'unsupported Eidos member {name}')

def _replace_super_refs(text, parent):
    out = []
    i = 0
    quote = None
    line_comment = False
    block_comment = False
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ''
        if block_comment:
            out.append(c)
            i += 1
            if c == ']' and n == '#':
                out.append(n)
                i += 1
                block_comment = False
            continue
        if line_comment:
            out.append(c)
            i += 1
            if c in '\r\n':
                line_comment = False
            continue
        if quote:
            out.append(c)
            i += 1
            if c == '\\' and i < len(text):
                out.append(text[i])
                i += 1
            elif c == quote:
                quote = None
            continue
        if c in ('"', "'"):
            quote = c
            out.append(c)
            i += 1
            continue
        if c == '#' and n == '[':
            out.extend(['#', '['])
            i += 2
            block_comment = True
            continue
        if c == '#':
            line_comment = True
            out.append(c)
            i += 1
            continue
        if c == '/' and n == '/':
            out.extend(['/', '/'])
            i += 2
            line_comment = True
            continue
        if text.startswith('super', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            end = i + 5
            if end == len(text) or not (text[end].isalnum() or text[end] == '_'):
                j = end
                while j < len(text) and text[j].isspace():
                    j += 1
                if j < len(text) and text[j] == '.':
                    j += 1
                    while j < len(text) and text[j].isspace():
                        j += 1
                    method = WORD.match(text, j)
                    if method:
                        k = method.end()
                        while k < len(text) and text[k].isspace():
                            k += 1
                        helper = f'{parent}__{method.group(0)}'
                        if k < len(text) and text[k] == '(':
                            out.append(f'{helper}(__eidos_obj')
                            i = k + 1
                            if i < len(text) and text[i] == ')':
                                continue
                            out.append(', ')
                            continue
                        out.append(f'{helper}(__eidos_obj)')
                        i = method.end()
                        continue
        out.append(c)
        i += 1
    return ''.join(out)


def _replace_field_refs(text, fields, params):
    out = []
    i = 0
    quote = None
    line_comment = False
    block_comment = False
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ''
        if block_comment:
            out.append(c)
            i += 1
            if c == ']' and n == '#':
                out.append(n)
                i += 1
                block_comment = False
            continue
        if line_comment:
            out.append(c)
            i += 1
            if c in '\r\n':
                line_comment = False
            continue
        if quote:
            out.append(c)
            i += 1
            if c == '\\' and i < len(text):
                out.append(text[i])
                i += 1
            elif c == quote:
                quote = None
            continue
        if c in ('"', "'"):
            quote = c
            out.append(c)
            i += 1
            continue
        if c == '#' and n == '[':
            out.extend(['#', '['])
            i += 2
            block_comment = True
            continue
        if c == '#':
            line_comment = True
            out.append(c)
            i += 1
            continue
        if c == '/' and n == '/':
            out.extend(['/', '/'])
            i += 2
            line_comment = True
            continue
        match = WORD.match(text, i)
        if match:
            word = match.group(0)
            prev = text[i - 1] if i else ''
            if word in fields and word not in params and prev != '.':
                out.append(f'__eidos_obj["{word}"]')
            else:
                out.append(word)
            i = match.end()
            continue
        out.append(c)
        i += 1
    return ''.join(out)

def _translate_eidos_decls(src):
    generated = []
    remainder = []
    class_defs = {}
    i = 0
    while i < len(src):
        start = None
        word = None
        j = i
        quote = None
        line_comment = False
        block_comment = False
        while j < len(src):
            c = src[j]
            n = src[j + 1] if j + 1 < len(src) else ''
            if block_comment:
                if c == ']' and n == '#':
                    block_comment = False
                    j += 2
                    continue
                j += 1
                continue
            if line_comment:
                if c in '\r\n':
                    line_comment = False
                j += 1
                continue
            if quote:
                if c == '\\':
                    j += 2
                    continue
                if c == quote:
                    quote = None
                j += 1
                continue
            if c in ('"', "'"):
                quote = c
                j += 1
                continue
            if c == '#' and n == '[':
                block_comment = True
                j += 2
                continue
            if c == '#':
                line_comment = True
                j += 1
                continue
            if c == '/' and n == '/':
                line_comment = True
                j += 2
                continue
            match = re.match(r'(eidos|ed)\b', src[j:])
            if match:
                start = j
                word = match.group(1)
                break
            j += 1
        if start is None:
            remainder.append(src[i:])
            break
        remainder.append(src[i:start])
        after = start + len(word)
        cursor = after
        while cursor < len(src) and src[cursor].isspace():
            cursor += 1
        name_match = WORD.match(src, cursor)
        if not name_match:
            remainder.append(src[start:after])
            i = after
            continue
        name = name_match.group(0)
        cursor = _skip_space_comments(src, name_match.end())
        parent = None
        if cursor < len(src) and src[cursor] == ':':
            cursor = _skip_space_comments(src, cursor + 1)
            parent_match = WORD.match(src, cursor)
            if not parent_match:
                raise ValueError(f'Eidos {name} inheritance needs a parent name')
            parent = parent_match.group(0)
            cursor = _skip_space_comments(src, parent_match.end())
            if cursor < len(src) and src[cursor] == '+':
                raise ValueError('Eidos mixins are not supported by the v0.4 desugar subset')
            if parent not in class_defs:
                raise ValueError(f'Eidos parent {parent} must be declared before {name}')
        if cursor >= len(src) or src[cursor] != '{':
            remainder.append(src[start:after])
            i = after
            continue
        end = _matching_brace(src, cursor)
        members = _parse_eidos_members(src[cursor + 1:end])
        own_fields = [m for m in members if m[0] == 'field']
        own_methods = [m for m in members if m[0] == 'method']
        inherited = class_defs.get(parent, {'fields': [], 'methods': {}})
        fields = list(inherited['fields'])
        field_indexes = {field[1]: index for index, field in enumerate(fields)}
        for field in own_fields:
            if field[1] in field_indexes:
                fields[field_indexes[field[1]]] = field
            else:
                field_indexes[field[1]] = len(fields)
                fields.append(field)
        effective_methods = dict(inherited['methods'])
        for method in own_methods:
            effective_methods[method[1]] = (name, method)
        helper_lines = []
        for method in own_methods:
            _, method_name, params, content, expression = method
            helper_name = f'{name}__{method_name}'
            all_fields = {field[1] for field in fields}
            body = _replace_field_refs(content, all_fields, set(params))
            if parent:
                body = _replace_super_refs(body, parent)
            if expression:
                body = f'return {body}'
            helper_params = ['__eidos_obj'] + params
            helper_lines.append(f'func {helper_name}({", ".join(helper_params)}) {{\n{body}\n}}\n')
        ctor_params = [f'__eidos_arg{i}' for i in range(len(fields))]
        factory = [f'func {name}({", ".join(ctor_params)}) {{', '    __eidos_obj = {}']
        seen_fields = []
        for index, (_, field_name, default) in enumerate(fields):
            initializer = _replace_field_refs(default, set(seen_fields), set())
            factory.append(f'    __eidos_obj["{field_name}"] = __eidos_arg{index} ?? ({initializer})')
            seen_fields.append(field_name)
        for method_name, (owner, method) in effective_methods.items():
            _, _, params, _, _ = method
            helper_name = f'{owner}__{method_name}'
            call_args = ['__eidos_obj'] + params
            if params:
                factory.append(f'    __eidos_obj["{method_name}"] = ({", ".join(params)} -> {helper_name}({", ".join(call_args)}))')
            else:
                factory.append(f'    __eidos_obj["{method_name}"] = (() -> {helper_name}({", ".join(call_args)}))')
        init_method = effective_methods.get('init')
        if init_method and not init_method[1][2]:
            factory.append('    __eidos_obj["init"]()')
        factory.extend(['    return __eidos_obj', '}', ''])
        generated.extend(helper_lines)
        generated.append('\n'.join(factory))
        class_defs[name] = {'fields': fields, 'methods': effective_methods}
        i = end + 1
    return ''.join(generated) + ''.join(remainder)

def _compose_code(src):
    """Desugar simple identifier composition while preserving strings/comments."""
    out=[]; i=0; quote=None; line_comment=False
    while i < len(src):
        c=src[i]
        if line_comment:
            out.append(c); i += 1
            if c in '\r\n': line_comment=False
            continue
        if quote:
            out.append(c); i += 1
            if c == '\\' and i < len(src): out.append(src[i]); i += 1
            elif c == quote: quote=None
            continue
        if c in ('"', "'"): quote=c; out.append(c); i += 1; continue
        if c == '#': line_comment=True; out.append(c); i += 1; continue
        m=re.match(r'([A-Za-z_][A-Za-z0-9_]*)\s*>>\s*([A-Za-z_][A-Za-z0-9_]*)', src[i:])
        if m:
            f,g=m.group(1),m.group(2)
            out.append(f'(x -> {g}({f}(x)))'); i += m.end(); continue
        out.append(c); i += 1
    return ''.join(out)

def translate(src):
    src = _translate_eidos_decls(src)
    out=[]; i=0; quote=None; line_comment=False
    while i < len(src):
        c=src[i]
        if line_comment:
            out.append(c); i += 1
            if c in '\r\n': line_comment=False
            continue
        if quote:
            out.append(c); i += 1
            if c == '\\' and i < len(src): out.append(src[i]); i += 1
            elif c == quote: quote=None
            continue
        if c in ('"', "'"):
            quote=c; out.append(c); i += 1; continue
        if c == '#' or (c == '/' and i + 1 < len(src) and src[i+1] == '/'):
            if c == '/': out.extend(['/', '/']); i += 2
            else: out.append(c); i += 1
            line_comment=True; continue
        m=WORD.match(src, i)
        if m:
            word=m.group(0); out.append('record' if word in ('eidos','ed') else word); i=m.end(); continue
        out.append(c); i += 1
    return _compose_code(''.join(out))

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('input'); ap.add_argument('output', nargs='?'); a=ap.parse_args()
    src=open(a.input, encoding='utf-8').read(); dst=translate(src)
    if a.output: open(a.output, 'w', encoding='utf-8', newline='').write(dst)
    else: print(dst, end='')
if __name__ == '__main__': main()
