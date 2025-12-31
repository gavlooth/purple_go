package parser

import (
	"fmt"
	"strconv"
	"strings"
	"unicode"

	"purple_go/pkg/ast"
)

// Parser parses S-expressions into Values
type Parser struct {
	input string
	pos   int
}

// New creates a new parser for the given input
func New(input string) *Parser {
	return &Parser{input: input, pos: 0}
}

// Parse parses a single S-expression
func (p *Parser) Parse() (*ast.Value, error) {
	p.skipWhitespace()
	if p.pos >= len(p.input) {
		return nil, nil
	}
	return p.parseExpr()
}

// ParseAll parses all S-expressions in the input
func (p *Parser) ParseAll() ([]*ast.Value, error) {
	var results []*ast.Value
	for {
		p.skipWhitespace()
		if p.pos >= len(p.input) {
			break
		}
		expr, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		if expr != nil {
			results = append(results, expr)
		}
	}
	return results, nil
}

func (p *Parser) skipWhitespace() {
	for p.pos < len(p.input) {
		ch := p.input[p.pos]
		if ch == ';' {
			// Skip comment to end of line
			for p.pos < len(p.input) && p.input[p.pos] != '\n' {
				p.pos++
			}
		} else if unicode.IsSpace(rune(ch)) {
			p.pos++
		} else {
			break
		}
	}
}

func (p *Parser) peek() byte {
	if p.pos >= len(p.input) {
		return 0
	}
	return p.input[p.pos]
}

func (p *Parser) advance() byte {
	ch := p.peek()
	if ch != 0 {
		p.pos++
	}
	return ch
}

func (p *Parser) parseExpr() (*ast.Value, error) {
	p.skipWhitespace()
	if p.pos >= len(p.input) {
		return nil, nil
	}

	ch := p.peek()

	switch ch {
	case '(':
		return p.parseList()
	case '\'':
		return p.parseQuote()
	case '`':
		return p.parseQuasiquote()
	case ',':
		return p.parseUnquote()
	case ')':
		return nil, fmt.Errorf("unexpected ')'")
	case '"':
		return p.parseString()
	default:
		return p.parseAtom()
	}
}

func (p *Parser) parseList() (*ast.Value, error) {
	p.advance() // consume '('
	var items []*ast.Value

	for {
		p.skipWhitespace()
		if p.pos >= len(p.input) {
			return nil, fmt.Errorf("unclosed list")
		}
		if p.peek() == ')' {
			p.advance()
			break
		}
		expr, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		items = append(items, expr)
	}

	return ast.SliceToList(items), nil
}

func (p *Parser) parseQuote() (*ast.Value, error) {
	p.advance() // consume '\''
	expr, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	if expr == nil {
		return nil, fmt.Errorf("expected expression after quote")
	}
	return ast.List2(ast.NewSym("quote"), expr), nil
}

func (p *Parser) parseQuasiquote() (*ast.Value, error) {
	p.advance() // consume '`'
	expr, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	if expr == nil {
		return nil, fmt.Errorf("expected expression after quasiquote")
	}
	return ast.List2(ast.NewSym("quasiquote"), expr), nil
}

func (p *Parser) parseUnquote() (*ast.Value, error) {
	p.advance() // consume ','
	// Check for unquote-splicing ,@
	if p.peek() == '@' {
		p.advance() // consume '@'
		expr, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		if expr == nil {
			return nil, fmt.Errorf("expected expression after unquote-splicing")
		}
		return ast.List2(ast.NewSym("unquote-splicing"), expr), nil
	}
	// Regular unquote
	expr, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	if expr == nil {
		return nil, fmt.Errorf("expected expression after unquote")
	}
	return ast.List2(ast.NewSym("unquote"), expr), nil
}

func (p *Parser) parseString() (*ast.Value, error) {
	p.advance() // consume opening '"'
	var sb strings.Builder

	for p.pos < len(p.input) {
		ch := p.advance()
		if ch == '"' {
			return ast.NewSym(sb.String()), nil
		}
		if ch == '\\' && p.pos < len(p.input) {
			next := p.advance()
			switch next {
			case 'n':
				sb.WriteByte('\n')
			case 't':
				sb.WriteByte('\t')
			case 'r':
				sb.WriteByte('\r')
			case '\\':
				sb.WriteByte('\\')
			case '"':
				sb.WriteByte('"')
			default:
				sb.WriteByte(next)
			}
		} else {
			sb.WriteByte(ch)
		}
	}
	return nil, fmt.Errorf("unclosed string")
}

func (p *Parser) parseAtom() (*ast.Value, error) {
	start := p.pos

	// Check for negative number
	if p.peek() == '-' && p.pos+1 < len(p.input) && isDigit(p.input[p.pos+1]) {
		p.advance()
	}

	// Check if it's a number
	if isDigit(p.peek()) {
		for p.pos < len(p.input) && isDigit(p.input[p.pos]) {
			p.pos++
		}
		numStr := p.input[start:p.pos]
		n, err := strconv.ParseInt(numStr, 10, 64)
		if err != nil {
			return nil, fmt.Errorf("invalid integer: %s", numStr)
		}
		return ast.NewInt(n), nil
	}

	// It's a symbol
	for p.pos < len(p.input) {
		ch := p.input[p.pos]
		if unicode.IsSpace(rune(ch)) || ch == '(' || ch == ')' || ch == '\'' || ch == '"' || ch == ';' || ch == '`' || ch == ',' {
			break
		}
		p.pos++
	}

	if p.pos == start {
		return nil, fmt.Errorf("unexpected character: %c", p.peek())
	}

	sym := p.input[start:p.pos]
	return ast.NewSym(sym), nil
}

func isDigit(ch byte) bool {
	return ch >= '0' && ch <= '9'
}

// ParseString is a convenience function to parse a string
func ParseString(input string) (*ast.Value, error) {
	p := New(input)
	return p.Parse()
}

// ParseAllString parses all expressions in a string
func ParseAllString(input string) ([]*ast.Value, error) {
	p := New(input)
	return p.ParseAll()
}
