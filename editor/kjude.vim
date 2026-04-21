" Vim syntax file for Kjude
" Language:     Kjude
" Maintainer:   Hamilnvt
" Last Change:  2026 Apr 03

if exists("b:current_syntax")
  finish
endif
let b:current_syntax = "kjude"

syntax clear
syntax case match

" Block comment /* … */
" syntax region kjComment
"       \ start="/\*"
"       \ end="\*/"
"       \ contains=kjTodo
"       \ fold

" Line comment // …
syntax match kjLineComment "//.*$"
      \ contains=kjTodo

syntax keyword kjTodo contained TODO FIXME XXX NOTE

syntax keyword kjControlFlow
      \ if elif else
      \ for while
      \ return

syntax keyword kjType
      \ void
      \ int uint bool string
      \ struct

syntax keyword kjConstant
      \ true false

syntax region kjString
      \ start=+"+  skip=+\\"+  end=+"+
      \ contains=kjEscape

syntax match kjNumber  "\<[1-9][0-9]*\>"

" syntax match kjOperator
"       \ "[-+*/%&|^~!<>=?:]"
syntax match kjOperator
      \ "[+*=]"

" syntax match kjOperator
"       \ "==\|!=\|<=\|>=\|&&\|||\|<<\|>>\|++\|--\|+=\|-=\|\*=\|/=\|%=\|&=\||=\|\^=\|<<=\|>>="
syntax match kjOperator
      \ "=="


" syntax keyword kjStdLib
"       \ alloc free

highlight default link kjLineComment    Comment
highlight default link kjTodo           Todo

highlight default link kjControlFlow    Conditional
highlight default link kjType           Type
highlight default link kjConstant       Constant

highlight default link kjString         String
highlight default link kjNumber         Number

highlight default link kjOperator       Operator

" highlight default link kjStdLib         Function
