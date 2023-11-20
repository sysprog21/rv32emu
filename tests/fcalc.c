/* fcalc - a basic, fully self-contained one-function calculator,
 * released into the public domain. written by Thomas Oltmann.
 *
 * Supported operations:
 *   addition (+)
 *   subtraction (-)
 *   multiplication (*)
 *   division (/)
 *   negation (-)
 * Has a builtin 'pi' constant
 *
 * Source: https://github.com/tomolt/fcalc
 */
double fcalc(const char **str, int *err, int _)
{
    switch (_) {
    case 0:; /* Parse full expression */
        const char *str1 = *str;
        int err1 = 0;
        double ret = fcalc(&str1, &err1, 1);
        while (*str1 != 0) {
            if (*str1 != ' ' && *str1 != '\n')
                err1 = 1;
            str1++;
        }
        if (err)
            *err = err1;
        return ret;
    case -1: /* Parse primitive */
        while (**str == ' ')
            (*str)++; /* Skip whitespace */
        switch (**str) {
        case '-': /* Parse unary minus (negation) */
            (*str)++;
            return -fcalc(str, err, -1);
        case '(': /* Parse subexpression in parentheses */
            (*str)++;
            double ret = fcalc(str, err, 1);
            if (**str != ')') {
                *err = -1;
                return 0.0;
            }
            (*str)++;
            return ret;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4': /* Parse number */
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '.':
            _ = 0;
            double dig = 0.0, bak = 1.0;
            for (;;) {
                if (**str == '.') {
                    _ = 1;
                    (*str)++;
                }
                if (!(**str >= '0' && **str <= '9'))
                    break;
                if (_ == 1)
                    bak /= 10.0;
                dig = dig * 10.0 + (**str - '0');
                (*str)++;
            }
            return dig * bak;
        case 'p':
        case 'P': /* Parse 'pi' */
            (*str)++;
            if (**str != 'i' && **str != 'I') {
                *err = -1;
                return 0.0;
            }
            (*str)++;
            return 3.14159265358979;
        default:
            *err = -1;
            return 0.0;
        }
    default:; /* Parse binary operation */
        double lhs = fcalc(str, err, -1);
        if (*err)
            return 0.0;
        for (;;) {
            while (**str == ' ')
                (*str)++; /* Skip whitespace */
#define BINOP(sym, prec, op)                      \
    if (prec >= _ && **str == sym) {              \
        (*str)++;                                 \
        double rhs = fcalc(str, err, (prec + 1)); \
        if (*err)                                 \
            return 0.0;                           \
        op;                                       \
    }
            /* clang-format off */
                 BINOP('+', 1, lhs += rhs)
            else BINOP('-', 1, lhs -= rhs)
            else BINOP('*', 2, lhs *= rhs)
            else BINOP('/', 2, lhs /= rhs)
            else return lhs;
            /* clang-format on */
#undef BINOP
        }
    }
}

#include <stdio.h>

static const double EPSILON = 0.00001;

static unsigned int fails, total;

static void test(const char *name, const char *str, double er, int ee)
{
    total++;
    int e;
    double r = fcalc(&str, &e, 0);
    double dr = r - er;
    if ((dr > 0.0 ? dr : -dr) > EPSILON || e != ee) {
        printf("Test '%s' failed; Expected %f/%d, got %f/%d\n", name, er, ee, r,
               e);
        fails++;
    }
}

int main()
{
    test("Integer", "11", 11, 0);
    test("Real number", "11.32", 11.32, 0);
    test("Sub-0-Real", ".32", 0.32, 0);
    test("Negation", "-42", -42, 0);
    test("Addition", "1.2 + 5", 6.2, 0);
    test("Subtraction", "3 - 1.4", 1.6, 0);
    test("Multiplication", "10 * 5", 50, 0);
    test("Division", "50 / 10", 5, 0);
    test("Precedence", "3 - 2 * 5", -7, 0);
    test("Pi", "pi", 3.14159265358979, 0);
    test("Parentheses", "3 * (1 + 2)", 9, 0);
    test("Unmatched (", "(1 + 2", 0.0, -1);
    printf("Performed %d tests, %d failures, %d%% success rate.\n", total,
           fails, 100 - fails * 100 / total);

    return fails == 0 ? 0 : -1;
}
