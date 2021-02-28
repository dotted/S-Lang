() = evalfile ("./common.sl");
require ("setfuns");

private define test_func (func, arglist, ans)
{
   variable ans1 = (@func)(__push_list(arglist));
   ifnot (_eqs (ans1, ans))
     {
	print (ans1, &ans1);
	print (ans, &ans);
	failed ("$func failed: got $ans1 instead of $ans"$);
     }
}

private define unique1 (a)
{
   variable i = unique (a);
   return a[i[array_sort(i)]];
}

private define test_setfuns ()
{
test_func (&unique1, {[1,2,2,3,5,-1]}, [1,2,3,5,-1]);
test_func (&unique1, {[1]}, [1]);
test_func (&unique1, {[1,1]}, [1]);
test_func (&unique1, {[0:-1]}, Int_Type[0]);
test_func (&unique1, {{1,2,2,3,5,-1}}, {1,2,3,5,-1});
test_func (&unique1, {{1}}, {1});
test_func (&unique1, {{1,1}}, {1});
test_func (&unique1, {{}}, {});

test_func (&intersection, {[1:5], [3:7]}, [2:4]);
test_func (&intersection, {[1:5], [5:7]}, [4]);
test_func (&intersection, {[1:5], [0:1]}, [0]);
test_func (&intersection, {[1:5], [5]}, [4]);
test_func (&intersection, {[1:5], [6]}, Int_Type[0]);

test_func (&intersection, {{1,2,3,4,5}, [3:7]}, [2:4]);
test_func (&intersection, {{1,2,3,4,5}, [5:7]}, [4]);
test_func (&intersection, {{1,2,3,4,5}, [0:1]}, [0]);
test_func (&intersection, {{1,2,3,4,5}, [5]}, [4]);
test_func (&intersection, {{1,2,3,4,5}, [6]}, Int_Type[0]);

#ifexists Complex_Type
test_func (&intersection, {{"foo", 2i, 3i}, {"foo", 3i}}, [0,2]);
test_func (&intersection, {{1, "foo", 2i, 3i}, {"foo", 3i}}, [1,3]);
test_func (&intersection, {{}, {"foo", 3i}}, Int_Type[0]);
test_func (&intersection, {{"foo", 3i},{}}, Int_Type[0]);
#endif
test_func (&intersection, {{"foo","foo"}, {"foo"}}, [0,1]);
test_func (&intersection, {{"foo"}, {"foo","bar"}}, [0]);

test_func (&complement, {{1,2,3,4,5}, [3:7]}, [0,1]);
test_func (&complement, {{1,2,3,4,5}, [5:7]}, [0:3]);
test_func (&complement, {{1,2,3,4,5}, [0:1]}, [1:4]);
test_func (&complement, {{1,2,3,4,5}, [5]}, [0:3]);
test_func (&complement, {{1,2,3,4,5}, [6]}, [0:4]);
#ifexists Complex_Type
test_func (&complement, {{"foo", 2i, 3i}, {"foo", 3i}}, [1]);
test_func (&complement, {{1, "foo", 2i, 3i}, {"foo", 3i}}, [0,2]);
test_func (&complement, {{}, {"foo", 3i}}, Int_Type[0]);
test_func (&complement, {{"foo", 3i}, {}}, [0,1]);
#endif
test_func (&complement, {{"foo","foo"}, {"foo"}}, Int_Type[0]);

test_func (&union, {{"foo", 1, 2}, {"bar", 1, 3}}, {"foo", 1, 2, "bar", 3});
test_func (&union, {[1:10], [3:5], [9:12]}, [1:12]);
#ifexists Complex_Type
test_func (&union, {[1:10], [3:5], 2i}, [[1:10], 2i]);
#endif
}

define slsh_main ()
{
   start_test ("setfuns");
   test_setfuns ();
   end_test ();
}
