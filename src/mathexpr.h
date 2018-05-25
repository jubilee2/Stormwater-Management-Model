/******************************************************************************
**  MODULE:        MATHEXPR.H
**  PROJECT:       SWMM 5.1
**  DESCRIPTION:   header file for the math expression parser in mathexpr.c.
**  AUTHORS:       L. Rossman, US EPA - NRMRL
**                 F. Shang, University of Cincinnati
**  VERSION:       5.1.001
**  LAST UPDATE:   03/20/14
******************************************************************************/

#ifndef MATHEXPR_H
#define MATHEXPR_H


//  Node in a tokenized math expression list
struct ExprNode
{
    int    opcode;                // operator code
    int    ivar;                  // variable index
    double fvalue;                // numerical value
	struct ExprNode *prev;        // previous node
    struct ExprNode *next;        // next node
};
typedef struct ExprNode MathExpr;

//  Creates a tokenized math expression from a string
MathExpr* mathexpr_create(SWMM_Project *sp, char* s,
        int (*getVar) (SWMM_Project *sp, char *));

//  Evaluates a tokenized math expression
double mathexpr_eval(SWMM_Project *sp, MathExpr* expr,
        double (*getVal) (SWMM_Project *sp, int));

//  Deletes a tokenized math expression
void  mathexpr_delete(MathExpr* expr);


// Local variables
//----------------
typedef struct
{
    int    Err;
    int    Bc;
    int    PrevLex, CurLex;
    int    Len, Pos;
    char   *S;
    char   Token[255];
    int    Ivar;
    double Fvalue;
} TMathexprShared;


#endif //MATHEXPR_H