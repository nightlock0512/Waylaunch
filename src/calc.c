#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "util.h"

// ==========================================
// 1. Lexer（字句解析）の定義
// ==========================================
typedef enum {
    TOK_NUM,    // 数値 (123)
    TOK_PLUS,   // +
    TOK_MINUS,  // -
    TOK_MUL,    // *
    TOK_DIV,    // /
    TOK_LPAREN, // (
    TOK_RPAREN, // )
    TOK_EOF     // 文字列の終わり
} TokenKind;

typedef struct {
    TokenKind kind;
    double val;    // TOK_NUM のときの値
} Token;

const char *src; // 入力文字列の現在位置
Token cur_tok;   // 現在見ているトークン（1トークン先読み）
static int calc_error = 0;

static void set_calc_error(const char *message) {
    if (debug_enabled && message != NULL) {
        fprintf(stderr, "%s\n", message);
    }
    calc_error = 1;
}

// 次のトークンを1つ切り出して cur_tok にセットする
void next_token() {
    if (calc_error) return;

    while (isspace((unsigned char)*src)) src++; // 空白スキップ

    if (*src == '\0') {
        cur_tok.kind = TOK_EOF;
        return;
    }
    if (isdigit((unsigned char)*src) || (*src == '.' && isdigit((unsigned char)src[1]))) {
        cur_tok.kind = TOK_NUM;
        cur_tok.val = strtod(src, (char **)&src);
        return;
    }

    switch (*src++) {
        case '+': cur_tok.kind = TOK_PLUS;   return;
        case '-': cur_tok.kind = TOK_MINUS;  return;
        case '*': cur_tok.kind = TOK_MUL;    return;
        case '/': cur_tok.kind = TOK_DIV;    return;
        case '(': cur_tok.kind = TOK_LPAREN; return;
        case ')': cur_tok.kind = TOK_RPAREN; return;
        default:
            if (debug_enabled) {
                fprintf(stderr, "Lexerエラー: 不明な文字 '%c'\n", *(src - 1));
            }
            calc_error = 1;
            return;
    }
}

// ==========================================
// 2. AST（抽象構文木）のノード定義
// ==========================================
typedef enum {
    ND_NUM, // 数値（葉ノード）
    ND_ADD, // +
    ND_SUB, // -
    ND_MUL, // *
    ND_DIV  // /
} NodeKind;

typedef struct Node {
    NodeKind kind;
    double val;        // ND_NUM のとき使う
    struct Node *lhs; // 左の子 (Left-Hand Side)
    struct Node *rhs; // 右の子 (Right-Hand Side)
    struct Node *next_allocated;
} Node;

static Node *allocated_nodes = NULL;

static void free_allocated_nodes(void) {
    while (allocated_nodes != NULL) {
        Node *node = allocated_nodes;
        allocated_nodes = node->next_allocated;
        free(node);
    }
}

// 演算子ノードを作るヘルパー
Node *new_node(NodeKind kind, Node *lhs, Node *rhs) {
    Node *node = calloc(1, sizeof(Node));
    if (node == NULL) return NULL;
    node->kind = kind;
    node->lhs = lhs;
    node->rhs = rhs;
    node->next_allocated = allocated_nodes;
    allocated_nodes = node;
    return node;
}

// 数値ノードを作るヘルパー
Node *new_num(double val) {
    Node *node = calloc(1, sizeof(Node));
    if (node == NULL) return NULL;
    node->kind = ND_NUM;
    node->val = val;
    node->next_allocated = allocated_nodes;
    allocated_nodes = node;
    return node;
}

// ==========================================
// 3. Parser（再帰下降構文解析）
// ==========================================
Node *parse_expr();
Node *parse_term();
Node *parse_unary();
Node *parse_primary();

// expr = term ( '+' term | '-' term )*
Node *parse_expr() {
    if (calc_error) return NULL;
    Node *node = parse_term();
    if (calc_error) return NULL;
    while (1) {
        if (cur_tok.kind == TOK_PLUS) {
            next_token();
            node = new_node(ND_ADD, node, parse_term());
        } else if (cur_tok.kind == TOK_MINUS) {
            next_token();
            node = new_node(ND_SUB, node, parse_term());
        } else {
            return node;
        }

        if (calc_error || node == NULL || node->lhs == NULL || node->rhs == NULL) {
            return NULL;
        }
    }
}

// term = unary ( '*' unary | '/' unary )*
Node *parse_term() {
    if (calc_error) return NULL;
    Node *node = parse_unary();
    if (calc_error) return NULL;
    while (1) {
        if (cur_tok.kind == TOK_MUL) {
            next_token();
            node = new_node(ND_MUL, node, parse_unary());
        } else if (cur_tok.kind == TOK_DIV) {
            next_token();
            node = new_node(ND_DIV, node, parse_unary());
        } else {
            return node;
        }

        if (calc_error || node == NULL || node->lhs == NULL || node->rhs == NULL) {
            return NULL;
        }
    }
}

// unary = '+' unary | '-' unary | primary
Node *parse_unary() {
    if (calc_error) return NULL;
    if (cur_tok.kind == TOK_PLUS) {
        next_token();
        return parse_unary();
    }
    if (cur_tok.kind == TOK_MINUS) {
        next_token();
        // "-x" は "0 - x" という木にしてしまうと後段の処理が楽になる！
        return new_node(ND_SUB, new_num(0), parse_unary());
    }
    return parse_primary();
}

// primary = '(' expr ')' | num
Node *parse_primary() {
    if (calc_error) return NULL;
    if (cur_tok.kind == TOK_LPAREN) {
        next_token(); // '(' を消費
        Node *node = parse_expr();
        if (calc_error) return NULL;
        if (cur_tok.kind != TOK_RPAREN) {
            set_calc_error("Parserエラー: ')' がありません");
            return NULL;
        }
        next_token(); // ')' を消費
        return node;
    }
    if (cur_tok.kind == TOK_NUM) {
        double val = cur_tok.val;
        next_token(); // 数値を消費
        return new_num(val);
    }
    set_calc_error("Parserエラー: 数値または '(' が必要です");
    return NULL;
}

// ==========================================
// 動作確認用：作った木を表示＆評価する
// ==========================================
void print_ast(Node *node, int depth) {
    if (!debug_enabled) {
        return;
    }
    for (int i = 0; i < depth; i++) printf("  ");
    switch (node->kind) {
        case ND_NUM: printf("%f\n", node->val); return;
        case ND_ADD: printf("+\n"); break;
        case ND_SUB: printf("-\n"); break;
        case ND_MUL: printf("*\n"); break;
        case ND_DIV: printf("/\n"); break;
    }
    print_ast(node->lhs, depth + 1);
    print_ast(node->rhs, depth + 1);
}

int parser(Node **tree) {
    calc_error = 0;
    next_token(); // 最初の1トークンを読み込む

    Node *ast = parse_expr();
    if (calc_error || ast == NULL || cur_tok.kind != TOK_EOF) {
        if (debug_enabled) {
            fprintf(stderr, "Parserエラー: 式の途中に余分なトークンがあります\n");
        }
        *tree = NULL;
        return 1;
    }

    // printf("入力式: %s\n\n--- 生成されたAST ---\n", src);
    // print_ast(ast, 0);
    *tree = ast;
    return 0;
}

double eval(Node *node) {
    if (node == NULL) {
        return 0.0;
    }

    // 葉ノード（数値）ならその値を返す
    if (node->kind == ND_NUM) {
        return node->val;
    }

    // 演算子ノードなら、左の子と右の子を再帰的に計算してから演算する
    double lhs = eval(node->lhs);
    double rhs = eval(node->rhs);

    switch (node->kind) {
        case ND_ADD: return lhs + rhs;
        case ND_SUB: return lhs - rhs;
        case ND_MUL: return lhs * rhs;
        case ND_DIV:
        if (rhs == 0.0) {
            if (debug_enabled) {
                fprintf(stderr, "実行エラー: 0で割ることはできません\n");
            }
            return 0.0;
        }
        return lhs / rhs;
        default:
        if (debug_enabled) {
            fprintf(stderr, "実行エラー: 未対応のノードです\n");
        }
        return 0.0;
    }
}

int calc(const char *src_text, double *result, char *error_msg, size_t error_size) {
    Node *tree = NULL;
    if (src_text == NULL || result == NULL) {
        if (error_msg != NULL && error_size > 0) {
            snprintf(error_msg, error_size, "Invalid expression");
        }
        return 0;
    }

    src = src_text;
    calc_error = 0;
    if (parser(&tree) != 0 || tree == NULL) {
        free_allocated_nodes();
        if (error_msg != NULL && error_size > 0) {
            snprintf(error_msg, error_size, "Invalid expression");
        }
        *result = 0.0;
        return 0;
    }

    *result = eval(tree);
    free_allocated_nodes();
    return 1;
}