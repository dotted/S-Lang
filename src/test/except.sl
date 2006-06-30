_debug_info = 1; () = evalfile ("inc.sl");
_traceback=1;
testing_feature ("exceptions");

private variable Exception_Line;
define test_exceptions ()
{
   variable e;
   variable e_caught = NULL;
   
   variable err_msg = "Caught testing, throwing MathError";
   variable err_obj = struct
     {
	something_silly
     };

   e_caught = NULL;
   try (e)
     {
	try (e)
	  {
	     Exception_Line = __LINE__; throw RunTimeError, "Testing...";
	  }
	catch RunTimeError:
	  {
	     if (e.line != Exception_Line)
	       failed ("e.line: %d != %d", e.line, Exception_Line);
	     Exception_Line = __LINE__; throw MathError, err_msg, err_obj;
	  }
     }
   catch MathError:
     {
	if (e.line != Exception_Line)
	  failed ("e.line: %d != %d", e.line, Exception_Line);
	e_caught = e;
     }
   
   if (e_caught.error != MathError)
     failed ("throw not MathError: %S", e_caught);
   
   if (e_caught.message != err_msg)
     failed ("throw MathError not message: %S", e_caught.message);
   if (e_caught.object != err_obj)
     failed ("throw MathError not object");


   variable finally_run = 0;

   variable count = 10;
   while (count)
     {
	count--;
	try (e)
	  {
	     () = 1+1;
	     if (count) continue;

	     Exception_Line = __LINE__; () = 1/0;
	  }
	catch MathError:
	  {
	     if (e.line != Exception_Line)
	       failed ("e.line: %d != %d", e.line, Exception_Line);
	     e_caught = MathError;
	  }
	finally
	  {
	     finally_run++;
	  }
     }
   
   if (e_caught != MathError)
     failed ("MathError");
   if (finally_run != 10)
     failed ("MathError Finally");

   finally_run = 0;
   e_caught = 0;
   variable bad_code_run = 0;

   try 
     {
	try
	  {
	     variable x = ();
	  }
	finally
	  {
	     finally_run = 1;
	  }
	failed ("This line should not have executed");
     }
   catch MathError;
   catch StackError;
   if (finally_run != 1)
     failed ("StackError finally not run");
   
}
test_exceptions ();

define stack_underflow ()
{
   try
     {
	variable x = ();
     }
   catch StackError;
}

define syntax_error ()
{
   variable num_evals = 0;

   try
     {
	try
	  {
	     eval ("define error ();");
	  }
	catch DuplicateDefinitionError;

	try
	  eval ("define foo () {loop (10) {x;");
	catch UndefinedNameError;

	try 
	  eval ("[1,2,3");
	catch ParseError;

	try eval ("("); catch ParseError; num_evals++;
	try eval ("("); catch AnyError; num_evals++;
     }
   catch ParseError;
   
   if (num_evals != 2)
     failed ("try eval: num_evals = %d", num_evals);
}
syntax_error ();
stack_underflow ();

private define test_try_with_return ()
{
   try
     {
	return 0;
     }
   catch MathError:
     {
	return -1;
     }
   
   return -2;
}

if (0 != test_try_with_return ())
  failed ("catch try with return");

private define test_catch_with_return (frun)
{
   
   @frun = 0;
   try
     {
	$1 = 1/0;
     }
   catch MathError:
     {
	return -1;
     }
   finally
     {
	loop (2)
	  @frun = @frun + 1;
     }
   return 2;
}

if ((-1 != test_catch_with_return (&$1)) or ($1 != 2))
  failed ("catch with return");

private define test_with_continue (frun)
{
   @frun = 0;
   loop (2)
     {
	try
	  {
	     return 1/0;
	  }
	catch MathError:
	  {
	     continue;
	  }
	finally
	  {
	     loop (2)
	       @frun = @frun + 1;
	  }
	return -1;		       %  not reached
     }
   return 1;
}

if ((1 != test_with_continue (&$1)) or ($1 != 4))
  failed ("catch with continue");

new_exception ("MyError", RunTimeError, "My error");
private define test_new_exception ()
{
   throw (MyError);
}
try
{
   test_new_exception ();
}
catch RunTimeError;
try
{
   test_new_exception ();
}
catch MyError;

#iffalse
try ($1)
{
   eval ("(&foo");
}
catch AnyError:
vmessage ("\n<<<%S>>>>", $1.message);
#endif

%---------------------------------------------------------------------------
% Old style exceptions
%---------------------------------------------------------------------------
define eb_stack_underflow ()
{
   ERROR_BLOCK
     {
	%vmessage ("Caught Stack underflow");
	_clear_error ();
     }
   
   variable x = ();
}

define eb_syntax_error ()
{
   ERROR_BLOCK
     {
	ERROR_BLOCK
	  {
	     _clear_error ();
	  }
	
	eval ("define error ();");
	eval ("define foo () {loop (10) {x;");
	eval ("[1,2,3");
	%vmessage ("Caught syntax error");
	_clear_error ();
     }
   eval ("(");
   eval ("(");
   eval ("(");
   eval ("(");
   eval ("(");
}

eb_syntax_error ();
eb_stack_underflow ();

print ("Ok\n");

exit (0);

