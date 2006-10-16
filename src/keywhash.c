/* Perfect hash generated by command line:
 * ./perfhash 1
 */
#ifndef SLCONST
#define SLCONST const
#endif
#define MIN_HASH_VALUE	2
#define MAX_HASH_VALUE	93
#define MIN_KEYWORD_LEN 2
#define MAX_KEYWORD_LEN 11

static SLCONST unsigned char Keyword_Hash_Table [256] =
{
   94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94, 
   94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94, 
   94,   0,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94, 
   18,   5,   7,   8,  11,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94, 
   94,  94,   0,   0,  94,   0,  94,  94,  94,   3,  94,   0,   0,  94,  94,   0, 
   94,  94,   0,   0,   0,   0,  94,  94,   0,  94,  94,  94,  94,  94,  94,   0, 
   94,   2,   5,  13,  12,   0,  17,   0,  23,  20,  94,   0,  17,   0,   0,   0, 
   29,  94,   0,   4,   0,   3,   0,   0,   1,   3,  94,  94,  94,  94,  94,  94, 
   94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94, 
   94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94, 
   94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94, 
   94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94, 
   94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94, 
   94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94, 
   94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94, 
   94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94,  94
};

static unsigned char keyword_hash (char *s, unsigned int len)
{
   unsigned int sum;

   sum = len;
   while (len)
     {
	len--;
	sum += (unsigned int) Keyword_Hash_Table [(unsigned char)s[len]];
     }
   return sum;
}

typedef SLCONST struct
{
   char *name;
   unsigned int type;
}
Keyword_Table_Type;

static Keyword_Table_Type Keyword_Table [/* 92 */] =
{
   {"or",	OR_TOKEN},
   {"not",	NOT_TOKEN},
   {"xor",	BXOR_TOKEN},
   {NULL,0},
   {"try",	TRY_TOKEN},
   {NULL,0},
   {NULL,0},
   {"return",	RETURN_TOKEN},
   {NULL,0},
   {"ERROR_BLOCK",	ERRBLK_TOKEN},
   {"break",	BREAK_TOKEN},
   {"EXIT_BLOCK",	EXITBLK_TOKEN},
   {"do",	DO_TOKEN},
   {"mod",	MOD_TOKEN},
   {"USER_BLOCK1",	USRBLK1_TOKEN},
   {"and",	AND_TOKEN},
   {"USER_BLOCK2",	USRBLK2_TOKEN},
   {"USER_BLOCK3",	USRBLK3_TOKEN},
   {"for",	FOR_TOKEN},
   {"_for",	_FOR_TOKEN},
   {"USER_BLOCK4",	USRBLK4_TOKEN},
   {"case",	CASE_TOKEN},
   {"forever",	FOREVER_TOKEN},
   {"else",	ELSE_TOKEN},
   {"struct",	STRUCT_TOKEN},
   {"orelse",	ORELSE_TOKEN},
   {"throw",	THROW_TOKEN},
   {"USER_BLOCK0",	USRBLK0_TOKEN},
   {"shr",	SHR_TOKEN},
   {NULL,0},
   {"using",	USING_TOKEN},
   {NULL,0},
   {"__tmp",	TMP_TOKEN},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {"if",	IF_TOKEN},
   {"!if",	IFNOT_TOKEN},
   {"exch",	EXCH_TOKEN},
   {"andelse",	ANDELSE_TOKEN},
   {NULL,0},
   {"continue",	CONT_TOKEN},
   {"static",	STATIC_TOKEN},
   {NULL,0},
   {"shl",	SHL_TOKEN},
   {NULL,0},
   {NULL,0},
   {"loop",	LOOP_TOKEN},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {"variable",	VARIABLE_TOKEN},
   {"define",	DEFINE_TOKEN},
   {"catch",	CATCH_TOKEN},
   {NULL,0},
   {"private",	PRIVATE_TOKEN},
   {NULL,0},
   {NULL,0},
   {"pop",	POP_TOKEN},
   {"foreach",	FOREACH_TOKEN},
   {NULL,0},
   {NULL,0},
   {"while",	WHILE_TOKEN},
   {"switch",	SWITCH_TOKEN},
   {NULL,0},
   {"typedef",	TYPEDEF_TOKEN},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {"finally",	FINALLY_TOKEN},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {NULL,0},
   {"public",	PUBLIC_TOKEN},
};

static Keyword_Table_Type *is_keyword (char *str, unsigned int len)
{
   unsigned int hash;
   char *name;
   Keyword_Table_Type *kw;

   if ((len < MIN_KEYWORD_LEN)
       || (len > MAX_KEYWORD_LEN))
     return NULL;

   hash = keyword_hash (str, len);
   if ((hash > MAX_HASH_VALUE) || (hash < MIN_HASH_VALUE))
     return NULL;

   kw = &Keyword_Table[hash - MIN_HASH_VALUE];
   if ((NULL != (name = kw->name))
       && (*str == *name)
       && (0 == strcmp (str, name)))
     return kw;
   return NULL;
}