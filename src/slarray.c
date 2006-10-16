/* Array manipulation routines for S-Lang */
/*
Copyright (C) 2004, 2005, 2006 John E. Davis

This file is part of the S-Lang Library.

The S-Lang Library is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The S-Lang Library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
USA.  
*/


#include "slinclud.h"

/* #define SL_APP_WANTS_FOREACH */
#include "slang.h"
#include "_slang.h"

typedef struct Range_Array_Type SLarray_Range_Array_Type;

struct Range_Array_Type
{
   SLindex_Type first_index;
   SLindex_Type last_index;
   SLindex_Type delta;
   int has_first_index;
   int has_last_index;
   int (*to_linear_fun) (SLang_Array_Type *, SLarray_Range_Array_Type *, VOID_STAR);
};

static SLang_Array_Type *inline_implicit_int_array (SLindex_Type *, SLindex_Type *, SLindex_Type *);

/* Use SLang_pop_array when a linear array is required. */
static int pop_array (SLang_Array_Type **at_ptr, int convert_scalar)
{
   SLang_Array_Type *at;
   SLindex_Type one = 1;
   int type;

   *at_ptr = NULL;
   type = SLang_peek_at_stack ();

   switch (type)
     {
      case -1:
	return -1;

      case SLANG_ARRAY_TYPE:
	return SLclass_pop_ptr_obj (SLANG_ARRAY_TYPE, (VOID_STAR *) at_ptr);

      case SLANG_NULL_TYPE:
	/* convert_scalar = 0; */  /* commented out for 2.0.5 to fix array_map NULL bug */
	/* drop */
      default:
	if (convert_scalar == 0)
	  {
	     SLdo_pop ();
	     SLang_verror (SL_TYPE_MISMATCH, "Context requires an array.  Scalar not converted");
	     return -1;
	  }
	break;
     }

   if (NULL == (at = SLang_create_array ((SLtype) type, 0, NULL, &one, 1)))
     return -1;

   if (at->flags & SLARR_DATA_VALUE_IS_POINTER)
     {
	/* The act of creating the array could have initialized the array
	 * with pointers to an object of the type.  For example, this could
	 * happen with user-defined structs.
	 */
	if (*(VOID_STAR *)at->data != NULL)
	  {
	     at->cl->cl_destroy ((SLtype) type, at->data);
	     *(VOID_STAR *) at->data = NULL;
	  }
     }

   if (-1 == at->cl->cl_apop ((SLtype) type, at->data))
     {
	SLang_free_array (at);
	return -1;
     }

   *at_ptr = at;

   return 0;
}

static VOID_STAR linear_get_data_addr (SLang_Array_Type *at, SLindex_Type *dims)
{
   size_t ofs;

   if (at->num_dims == 1)
     {
	if (*dims < 0)
	  ofs = (size_t) (*dims + at->dims[0]);
	else
	  ofs = (size_t)*dims;
     }
   else 
     {
	unsigned int i;
	SLindex_Type *max_dims = at->dims;
	unsigned int num_dims = at->num_dims;
	ofs = 0;
	for (i = 0; i < num_dims; i++)
	  {
	     SLindex_Type d = dims[i];
	     
	     if (d < 0)
	       d = d + max_dims[i];
	     
	     ofs = ofs * (size_t)max_dims [i] + (size_t) d;
	  }
     }
   if (ofs >= at->num_elements)
     {
	SLang_set_error (SL_Index_Error);
	return NULL;
     }
   return (VOID_STAR) ((char *)at->data + (ofs * at->sizeof_type));
}

_INLINE_
static VOID_STAR get_data_addr (SLang_Array_Type *at, SLindex_Type *dims)
{
   VOID_STAR data;

   data = at->data;
   if (data == NULL)
     {
	SLang_verror (SL_UNKNOWN_ERROR, "Array has no data");
	return NULL;
     }

   data = (*at->index_fun) (at, dims);

   if (data == NULL)
     {
	SLang_verror (SL_UNKNOWN_ERROR, "Unable to access array element");
	return NULL;
     }

   return data;
}

void _pSLarray_free_array_elements (SLang_Class_Type *cl, VOID_STAR s, SLuindex_Type num)
{
   size_t sizeof_type;
   void (*f) (SLtype, VOID_STAR);
   char *p;
   SLtype type;

   if ((cl->cl_class_type == SLANG_CLASS_TYPE_SCALAR)
       || (cl->cl_class_type == SLANG_CLASS_TYPE_VECTOR))
     return;

   f = cl->cl_destroy;
   sizeof_type = cl->cl_sizeof_type;
   type = cl->cl_data_type;

   p = (char *) s;
   while (num != 0)
     {
	if (NULL != *(VOID_STAR *)p)
	  {
	     (*f) (type, (VOID_STAR)p);
	     *(VOID_STAR *) p = NULL;
	  }
	p += sizeof_type;
	num--;
     }
}

static int destroy_element (SLang_Array_Type *at,
			    SLindex_Type *dims,
			    VOID_STAR data)
{
   data = get_data_addr (at, dims);
   if (data == NULL)
     return -1;

   /* This function should only get called for arrays that have
    * pointer elements.  Do not call the destroy method if the element
    * is NULL.
    */
   if (NULL != *(VOID_STAR *)data)
     {
	(*at->cl->cl_destroy) (at->data_type, data);
	*(VOID_STAR *) data = NULL;
     }
   return 0;
}

/* This function only gets called when a new array is created.  Thus there
 * is no need to destroy the object first.
 */
static int new_object_element (SLang_Array_Type *at,
			       SLindex_Type *dims,
			       VOID_STAR data)
{
   data = get_data_addr (at, dims);
   if (data == NULL)
     return -1;

   return (*at->cl->cl_init_array_object) (at->data_type, data);
}

int _pSLarray_next_index (SLindex_Type *dims, SLindex_Type *max_dims, unsigned int num_dims)
{
   while (num_dims)
     {
	SLindex_Type dims_i;

	num_dims--;

	dims_i = dims [num_dims] + 1;
	if (dims_i < (int) max_dims [num_dims])
	  {
	     dims [num_dims] = dims_i;
	     return 0;
	  }
	dims [num_dims] = 0;
     }

   return -1;
}

static int do_method_for_all_elements (SLang_Array_Type *at,
				       int (*method)(SLang_Array_Type *,
						     SLindex_Type *,
						     VOID_STAR),
				       VOID_STAR client_data)
{
   SLindex_Type dims [SLARRAY_MAX_DIMS];
   SLindex_Type *max_dims;
   unsigned int num_dims;

   if (at->num_elements == 0)
     return 0;

   max_dims = at->dims;
   num_dims = at->num_dims;

   SLMEMSET((char *)dims, 0, sizeof(dims));

   do
     {
	if (-1 == (*method) (at, dims, client_data))
	  return -1;
     }
   while (0 == _pSLarray_next_index (dims, max_dims, num_dims));

   return 0;
}

void SLang_free_array (SLang_Array_Type *at)
{
   unsigned int flags;

   if (at == NULL) return;

   if (at->num_refs > 1)
     {
	at->num_refs -= 1;
	return;
     }

   flags = at->flags;

   if (flags & SLARR_DATA_VALUE_IS_INTRINSIC)
     return;			       /* not to be freed */

   if (flags & SLARR_DATA_VALUE_IS_POINTER)
     (void) do_method_for_all_elements (at, destroy_element, NULL);

   if (at->free_fun != NULL)
     at->free_fun (at);
   else
     SLfree ((char *) at->data);

   SLfree ((char *) at);
}

SLang_Array_Type *
SLang_create_array1 (SLtype type, int read_only, VOID_STAR data,
		     SLindex_Type *dims, unsigned int num_dims, int no_init)
{
   SLang_Class_Type *cl;
   SLang_Array_Type *at;
   SLuindex_Type i, num_elements;
   size_t sizeof_type;
   size_t size;

   if (num_dims > SLARRAY_MAX_DIMS)
     {
	SLang_verror (SL_NOT_IMPLEMENTED, "%u dimensional arrays are not supported", num_dims);
	return NULL;
     }

   for (i = 0; i < num_dims; i++)
     {
	if (dims[i] < 0)
	  {
	     SLang_verror (SL_INVALID_PARM, "Size of array dim %u is less than 0", i);
	     return NULL;
	  }
     }

   cl = _pSLclass_get_class (type);

   at = (SLang_Array_Type *) SLmalloc (sizeof(SLang_Array_Type));
   if (at == NULL)
     return NULL;

   memset ((char*) at, 0, sizeof(SLang_Array_Type));

   at->data_type = type;
   at->cl = cl;
   at->num_dims = num_dims;
   at->num_refs = 1;

   if (read_only) at->flags = SLARR_DATA_VALUE_IS_READ_ONLY;

   if ((cl->cl_class_type != SLANG_CLASS_TYPE_SCALAR)
       && (cl->cl_class_type != SLANG_CLASS_TYPE_VECTOR))
     at->flags |= SLARR_DATA_VALUE_IS_POINTER;

   num_elements = 1;
   for (i = 0; i < num_dims; i++)
     {
	at->dims[i] = dims[i];
	num_elements = dims[i] * num_elements;
     }

   /* Now set the rest of the unused dimensions to 1.  This makes it easier
    * when transposing arrays.
    */
   while (i < SLARRAY_MAX_DIMS)
     at->dims[i++] = 1;

   at->num_elements = num_elements;
   at->index_fun = linear_get_data_addr;
   at->sizeof_type = sizeof_type = cl->cl_sizeof_type;

   if (data != NULL)
     {
	at->data = data;
	return at;
     }

   size = (size_t) (num_elements * sizeof_type);
   if (size/sizeof_type != num_elements)
     {
	SLang_verror (SL_INVALID_PARM, "Unable to create array of the desired size");
	SLang_free_array (at);
	return NULL;
     }

   if (size == 0) size = 1;

   if (NULL == (data = (VOID_STAR) SLmalloc (size)))
     {
	SLang_free_array (at);
	return NULL;
     }

   at->data = data;

   if ((no_init == 0) || (at->flags & SLARR_DATA_VALUE_IS_POINTER))
     memset ((char *) data, 0, size);
   
   if ((no_init == 0)
       && (cl->cl_init_array_object != NULL)
       && (-1 == do_method_for_all_elements (at, new_object_element, NULL)))
     {
	SLang_free_array (at);
	return NULL;
     }

   return at;
}

SLang_Array_Type *
SLang_create_array (SLtype type, int read_only, VOID_STAR data,
		    SLindex_Type *dims, unsigned int num_dims)
{
   return SLang_create_array1 (type, read_only, data, dims, num_dims, 0);
}

int SLang_add_intrinsic_array (char *name,
			       SLtype type,
			       int read_only,
			       VOID_STAR data,
			       unsigned int num_dims, ...)
{
   va_list ap;
   unsigned int i;
   SLindex_Type dims[SLARRAY_MAX_DIMS];
   SLang_Array_Type *at;

   if ((num_dims > SLARRAY_MAX_DIMS)
       || (name == NULL)
       || (data == NULL))
     {
	SLang_verror (SL_INVALID_PARM, "Unable to create intrinsic array");
	return -1;
     }

   va_start (ap, num_dims);
   for (i = 0; i < num_dims; i++)
     dims [i] = va_arg (ap, int);
   va_end (ap);

   at = SLang_create_array (type, read_only, data, dims, num_dims);
   if (at == NULL)
     return -1;
   at->flags |= SLARR_DATA_VALUE_IS_INTRINSIC;

   /* Note: The variable that refers to the intrinsic array is regarded as
    * read-only.  That way, Array_Name = another_array; will fail.
    */
   if (-1 == SLadd_intrinsic_variable (name, (VOID_STAR) at, SLANG_ARRAY_TYPE, 1))
     {
	SLang_free_array (at);
	return -1;
     }
   return 0;
}

static int pop_array_indices (SLindex_Type *dims, unsigned int num_dims)
{
   unsigned int n;
   int i;

   if (num_dims > SLARRAY_MAX_DIMS)
     {
	SLang_verror (SL_INVALID_PARM, "Array size not supported");
	return -1;
     }

   n = num_dims;
   while (n != 0)
     {
	n--;
	if (-1 == SLang_pop_integer (&i))
	  return -1;

	dims[n] = i;
     }

   return 0;
}

int SLang_push_array (SLang_Array_Type *at, int free_flag)
{
   if (at == NULL)
     return SLang_push_null ();

   at->num_refs += 1;

   if (0 == SLclass_push_ptr_obj (SLANG_ARRAY_TYPE, (VOID_STAR) at))
     {
	if (free_flag)
	  SLang_free_array (at);
	return 0;
     }

   at->num_refs -= 1;

   if (free_flag) SLang_free_array (at);
   return -1;
}

/* This function gets called via expressions such as Double_Type[10, 20];
 */
static int push_create_new_array (unsigned int num_dims)
{
   SLang_Array_Type *at;
   SLtype type;
   SLindex_Type dims [SLARRAY_MAX_DIMS];
   int (*anew) (SLtype, unsigned int);

   if (-1 == SLang_pop_datatype (&type))
     return -1;

   anew = (_pSLclass_get_class (type))->cl_anew;
   if (anew != NULL)
     return (*anew) (type, num_dims);

   if (-1 == pop_array_indices (dims, num_dims))
     return -1;

   if (NULL == (at = SLang_create_array (type, 0, NULL, dims, num_dims)))
     return -1;

   return SLang_push_array (at, 1);
}

static int push_element_at_addr (SLang_Array_Type *at,
				 VOID_STAR data, int allow_null)
{
   SLang_Class_Type *cl;

   cl = at->cl;
   if ((at->flags & SLARR_DATA_VALUE_IS_POINTER)
       && (*(VOID_STAR *) data == NULL))
     {
	if (allow_null)
	  return SLang_push_null ();

	SLang_verror (SL_VARIABLE_UNINITIALIZED,
		      "%s array has uninitialized element", cl->cl_name);
	return -1;
     }

   return (*cl->cl_apush)(at->data_type, data);
}

static int coerse_array_to_linear (SLang_Array_Type *at)
{
   SLarray_Range_Array_Type *range;
   VOID_STAR vdata;
   SLuindex_Type imax;

   /* FIXME: Priority = low.  This assumes that if an array is not linear, then
    * it is a range.
    */
   if (0 == (at->flags & SLARR_DATA_VALUE_IS_RANGE))
     return 0;

   range = (SLarray_Range_Array_Type *) at->data;
   if ((range->has_last_index == 0) || (range->has_first_index == 0))
     {
	SLang_verror (SL_INVALID_PARM, "Invalid context for a range array of indeterminate size");
	return -1;
     }

   imax = at->num_elements;
   vdata = (VOID_STAR) SLmalloc ((imax + 1) * at->sizeof_type);
   if (vdata == NULL)
     return -1;
   (void) (*range->to_linear_fun)(at, range, vdata);
   SLfree ((char *) range);
   at->data = (VOID_STAR) vdata;
   at->flags &= ~SLARR_DATA_VALUE_IS_RANGE;
   at->index_fun = linear_get_data_addr;
   return 0;
}

static void
free_index_objects (SLang_Object_Type *index_objs, unsigned int num_indices)
{
   unsigned int i;
   SLang_Object_Type *obj;

   for (i = 0; i < num_indices; i++)
     {
	obj = index_objs + i;
	if (obj->data_type != 0)
	  SLang_free_object (obj);
     }
}

static int
pop_indices (SLang_Array_Type *at_to_index,
	     SLang_Object_Type *index_objs, unsigned int num_indices,
	     int *is_index_array)
{
   unsigned int i;

   memset((char *) index_objs, 0, num_indices * sizeof (SLang_Object_Type));

   *is_index_array = 0;

   if (num_indices != at_to_index->num_dims)
     {
	if (num_indices != 1)	       /* when 1, it is an index array */
	  {
	     SLang_verror (SL_INVALID_PARM, "wrong number of indices for array");
	     return -1;
	  }
     }

   i = num_indices;
   while (i != 0)
     {
	SLang_Object_Type *obj;
	SLtype data_type;
	SLang_Array_Type *at;

	i--;
	obj = index_objs + i;
	if (SLANG_ARRAY_TYPE != _pSLang_peek_at_stack2 (&data_type))
	  {
	     if (-1 == _pSLang_pop_object_of_type (SLANG_ARRAY_INDEX_TYPE, obj, 0))
	       goto return_error;
	     
	     continue;
	  }
	if (data_type != SLANG_ARRAY_INDEX_TYPE)
	  {
	     if (-1 == SLclass_typecast (SLANG_ARRAY_INDEX_TYPE, 1, 1))
	       return -1;
	  }
	if (-1 == SLang_pop (obj))
	  goto return_error;

	at = obj->v.array_val;
	if (at->flags & SLARR_DATA_VALUE_IS_RANGE)
	  {
	     SLarray_Range_Array_Type *r = (SLarray_Range_Array_Type *) at->data;
	     if ((r->has_last_index == 0) || (r->has_first_index == 0))
	       {
		  /* Cases to consider (positive increment)
		   *   [:]  ==> [0:n-1] all elements
		   *   [:i] ==> [0:i] for i>=0, [0:n+i] for i<0
		   *   [i:] ==> [i:n-1] for i>=0, [i+n:n-1] for i<0
		   * This will allow: [:-3] to index all but last 3, etc.
		   * Also consider cases with a negative increment:
		   *   [::-1] = [n-1,n-2,...0] = [n-1:0:-1]
		   *   [:i:-1] = [n-1,n-2,..i] = [n-1:i:-1]
		   *   [i::-1] = [i,i-1,...0] = [i:0:-1] 
		   */
		  SLang_Array_Type *new_at;
		  SLindex_Type first_index, last_index;
		  SLindex_Type delta = r->delta;
		  SLindex_Type n;

		  if (num_indices == 1)/* could be index array */
		    n = (SLindex_Type)at_to_index->num_elements;
		  else
		    n = at_to_index->dims[i];

		  if (r->has_first_index)
		    {
		       /* Case 3 */
		       first_index = r->first_index;
		       if (first_index < 0) first_index += n;
		       if (delta > 0) last_index = n-1; else last_index = 0;
		    }
		  else if (r->has_last_index)
		    {
		       /* case 2 */
		       if (delta > 0) first_index = 0; else first_index = n-1;
		       last_index = r->last_index;
		       if (last_index < 0)
			 last_index += n;
		    }
		  else
		    {
		       /* case 0 */
		       if (delta > 0)
			 {
			    first_index = 0;
			    last_index = n - 1;
			 }
		       else
			 {
			    first_index = n-1;
			    last_index = 0;
			 }
		    }

		  if (NULL == (new_at = inline_implicit_int_array (&first_index, &last_index, &delta)))
		    goto return_error;
	     
		  SLang_free_array (at);
		  obj->v.array_val = new_at;
	       }
	  }
	if (num_indices == 1)
	  {
	     *is_index_array = 1;
	     return 0;
	  }
     }
   return 0;

   return_error:
   free_index_objects (index_objs, num_indices);
   return -1;
}

static void do_index_error (SLindex_Type i, SLindex_Type indx, SLindex_Type dim)
{
   SLang_verror (SL_Index_Error, "Array index %u (value=%ld) out of allowed range 0<=index<%ld",
		 i, (long)indx, (long)dim);
}

static int
transfer_n_elements (SLang_Array_Type *at, VOID_STAR dest_data, VOID_STAR src_data,
		     size_t sizeof_type, SLuindex_Type n, int is_ptr)
{
   SLtype data_type;
   SLang_Class_Type *cl;

   if (is_ptr == 0)
     {
	SLMEMCPY ((char *) dest_data, (char *)src_data, n * sizeof_type);
	return 0;
     }

   data_type = at->data_type;
   cl = at->cl;

   while (n != 0)
     {
	if (*(VOID_STAR *)dest_data != NULL)
	  {
	     (*cl->cl_destroy) (data_type, dest_data);
	     *(VOID_STAR *) dest_data = NULL;
	  }

	if (*(VOID_STAR *) src_data == NULL)
	  *(VOID_STAR *) dest_data = NULL;
	else
	  {
	     if (-1 == (*cl->cl_acopy) (data_type, src_data, dest_data))
	       /* No need to destroy anything */
	       return -1;
	  }

	src_data = (VOID_STAR) ((char *)src_data + sizeof_type);
	dest_data = (VOID_STAR) ((char *)dest_data + sizeof_type);

	n--;
     }

   return 0;
}

_INLINE_
int
_pSLarray_aget_transfer_elem (SLang_Array_Type *at, SLindex_Type *indices,
			     VOID_STAR new_data, size_t sizeof_type, int is_ptr)
{
   VOID_STAR at_data;

   /* Since 1 element is being transferred, there is no need to coerce
    * the array to linear.
    */
   if (NULL == (at_data = get_data_addr (at, indices)))
     return -1;

   if (is_ptr == 0)
     {
	memcpy ((char *) new_data, (char *)at_data, sizeof_type);
	return 0;
     }

   return transfer_n_elements (at, new_data, at_data, sizeof_type, 1, is_ptr);
}

#if SLANG_OPTIMIZE_FOR_SPEED
static int aget_doubles_from_index_array (double *src_data, SLindex_Type num_elements,
					  SLindex_Type *indices, SLindex_Type *indices_max,
					  double *dest_data)
{
   while (indices < indices_max)
     {
	SLindex_Type i = *indices;

	if (i < 0) 
	  {
	     i += num_elements;
	     if (i < 0)
	       i = num_elements;
	  }
	if (i >= num_elements)
	  {
	     SLang_set_error (SL_Index_Error);
	     return -1;
	  }
	*dest_data++ = src_data[i];
	indices++;
     }
   return 0;
}

static int aget_floats_from_index_array (float *src_data, SLindex_Type num_elements,
					 SLindex_Type *indices, SLindex_Type *indices_max,
					 float *dest_data)
{
   while (indices < indices_max)
     {
	SLindex_Type i = *indices;

	if (i < 0) 
	  {
	     i += num_elements;
	     if (i < 0)
	       i = num_elements;
	  }
	if (i >= num_elements)
	  {
	     SLang_set_error (SL_Index_Error);
	     return -1;
	  }
	*dest_data++ = src_data[i];
	indices++;
     }
   return 0;
}

static int aget_ints_from_index_array (int *src_data, SLindex_Type num_elements,
				       SLindex_Type *indices, SLindex_Type *indices_max,
				       int *dest_data)
{
   while (indices < indices_max)
     {
	SLindex_Type i = *indices;

	if (i < 0) 
	  {
	     i += num_elements;
	     if (i < 0)
	       i = num_elements;
	  }
	if (i >= num_elements)
	  {
	     SLang_set_error (SL_Index_Error);
	     return -1;
	  }
	*dest_data++ = src_data[i];
	indices++;
     }
   return 0;
}
#endif


/* Here the ind_at index-array is an n-d array of indices.  This function
 * creates an n-d array of made up of values of 'at' at the locations
 * specified by the indices.  The result is pushed.
 */
static int
aget_from_index_array (SLang_Array_Type *at,
		       SLang_Array_Type *ind_at)
{
   SLang_Array_Type *new_at;
   SLindex_Type *indices, *indices_max, num_elements;
   unsigned char *new_data, *src_data;
   size_t sizeof_type;
   int is_ptr;
   
   if (-1 == coerse_array_to_linear (at))
     return -1;

   if (-1 == coerse_array_to_linear (ind_at))
     return -1;

   if (NULL == (new_at = SLang_create_array (at->data_type, 0, NULL, ind_at->dims, ind_at->num_dims)))
     return -1;

   /* Since the index array is linear, I can address it directly */
   indices = (SLindex_Type *) ind_at->data;
   indices_max = indices + ind_at->num_elements;

   src_data = (unsigned char *) at->data;
   new_data = (unsigned char *) new_at->data;
   num_elements = (SLindex_Type) at->num_elements;
   sizeof_type = new_at->sizeof_type;
   is_ptr = (new_at->flags & SLARR_DATA_VALUE_IS_POINTER);

   switch (at->data_type)
     {
#if SLANG_OPTIMIZE_FOR_SPEED
# if SLANG_HAS_FLOAT
      case SLANG_DOUBLE_TYPE:
	if (-1 == aget_doubles_from_index_array ((double *)src_data, num_elements,
						 indices, indices_max,
						 (double *)new_data))
	  goto return_error;
	break;
      case SLANG_FLOAT_TYPE:
	if (-1 == aget_floats_from_index_array ((float *)src_data, num_elements,
						indices, indices_max,
						(float *)new_data))
	  goto return_error;
	break;
# endif
      case SLANG_INT_TYPE:
	if (-1 == aget_ints_from_index_array ((int *)src_data, num_elements,
					      indices, indices_max,
					      (int *)new_data))
	  goto return_error;
	break;
#endif
      default:
	while (indices < indices_max)
	  {
	     size_t offset;
	     SLindex_Type i = *indices;

	     if (i < 0) 
	       {
		  i += num_elements;
		  if (i < 0)
		    i = num_elements;
	       }
	     if (i >= num_elements)
	       {
		  SLang_set_error (SL_Index_Error);
		  goto return_error;
	       }

	     offset = sizeof_type * (SLuindex_Type)i;
	     if (-1 == transfer_n_elements (at, (VOID_STAR) new_data,
					    (VOID_STAR) (src_data + offset),
					    sizeof_type, 1, is_ptr))
	       goto return_error;

	     new_data += sizeof_type;
	     indices++;
	  }
     }

   return SLang_push_array (new_at, 1);
   
   return_error:
   SLang_free_array (new_at);
   return -1;
}

/* This is extremely ugly.  It is due to the fact that the index_objects
 * may contain ranges.  This is a utility function for the aget/aput
 * routines
 */
static int
convert_nasty_index_objs (SLang_Array_Type *at,
			  SLang_Object_Type *index_objs,
			  unsigned int num_indices,
			  SLindex_Type **index_data,
			  SLindex_Type *range_buf, SLindex_Type *range_delta_buf,
			  SLindex_Type *max_dims,
			  unsigned int *num_elements,
			  int *is_array, int is_dim_array[SLARRAY_MAX_DIMS])
{
   unsigned int i, total_num_elements;
   SLang_Array_Type *ind_at;

   if (num_indices != at->num_dims)
     {
	SLang_verror (SL_INVALID_PARM, "Array requires %u indices", at->num_dims);
	return -1;
     }

   *is_array = 0;
   total_num_elements = 1;
   for (i = 0; i < num_indices; i++)
     {
	SLang_Object_Type *obj = index_objs + i;
	range_delta_buf [i] = 0;

	if (obj->data_type == SLANG_ARRAY_INDEX_TYPE)
	  {
	     range_buf [i] = obj->v.index_val;
	     max_dims [i] = 1;
	     index_data[i] = range_buf + i;
	     is_dim_array[i] = 0;
	  }
#if SLANG_ARRAY_INDEX_TYPE != SLANG_INT_TYPE
	else if (obj->data_type == SLANG_INT_TYPE)
	  {
	     range_buf [i] = obj->v.int_val;
	     max_dims [i] = 1;
	     index_data[i] = range_buf + i;
	     is_dim_array[i] = 0;
	  }
#endif
	else
	  {
	     *is_array = 1;
	     is_dim_array[i] = 1;
	     ind_at = obj->v.array_val;

	     if (ind_at->flags & SLARR_DATA_VALUE_IS_RANGE)
	       {
		  SLarray_Range_Array_Type *r;

		  r = (SLarray_Range_Array_Type *) ind_at->data;
		  range_buf[i] = r->first_index;
		  range_delta_buf [i] = r->delta;
		  max_dims[i] = (SLindex_Type) ind_at->num_elements;
	       }
	     else
	       {
		  index_data [i] = (SLindex_Type *) ind_at->data;
		  max_dims[i] = (SLindex_Type) ind_at->num_elements;
	       }
	  }

	total_num_elements = total_num_elements * max_dims[i];
     }

   *num_elements = total_num_elements;
   return 0;
}

/* This routine pushes a 1-d vector of values from 'at' indexed by
 * the objects 'index_objs'.  These objects can either be integers or
 * 1-d integer arrays.  The fact that the 1-d arrays can be ranges
 * makes this look ugly.
 */
static int
aget_from_indices (SLang_Array_Type *at,
		   SLang_Object_Type *index_objs, unsigned int num_indices)
{
   SLindex_Type *index_data [SLARRAY_MAX_DIMS];
   SLindex_Type range_buf [SLARRAY_MAX_DIMS];
   SLindex_Type range_delta_buf [SLARRAY_MAX_DIMS];
   SLindex_Type max_dims [SLARRAY_MAX_DIMS];
   unsigned int i, num_elements;
   SLang_Array_Type *new_at;
   SLindex_Type map_indices[SLARRAY_MAX_DIMS];
   SLindex_Type indices [SLARRAY_MAX_DIMS];
   SLindex_Type *at_dims;
   size_t sizeof_type;
   int is_ptr, ret, is_array;
   char *new_data;
   SLang_Class_Type *cl;
   int is_dim_array[SLARRAY_MAX_DIMS];

   if (-1 == convert_nasty_index_objs (at, index_objs, num_indices,
				       index_data, range_buf, range_delta_buf,
				       max_dims, &num_elements, &is_array,
				       is_dim_array))
     return -1;

   is_ptr = (at->flags & SLARR_DATA_VALUE_IS_POINTER);
   sizeof_type = at->sizeof_type;

   cl = _pSLclass_get_class (at->data_type);

   if ((is_array == 0) && (num_elements == 1))
     {
	new_data = (char *)cl->cl_transfer_buf;
	memset (new_data, 0, sizeof_type);
	new_at = NULL;
     }
   else
     {
	SLindex_Type i_num_elements = (SLindex_Type)num_elements;

	new_at = SLang_create_array (at->data_type, 0, NULL, &i_num_elements, 1);
	if (NULL == new_at)
	  return -1;
	if (num_elements == 0)
	  return SLang_push_array (new_at, 1);

	new_data = (char *)new_at->data;
     }
   
   at_dims = at->dims;
   memset ((char *) map_indices, 0, sizeof(map_indices));
   while (1)
     {
	for (i = 0; i < num_indices; i++)
	  {
	     SLindex_Type j = map_indices[i];
	     SLindex_Type indx;

	     if (0 != range_delta_buf[i])
	       indx = range_buf[i] + j * range_delta_buf[i];
	     else
	       indx = index_data [i][j];

	     if (indx < 0)
	       indx += at_dims[i];

	     if ((indx < 0) || (indx >= at_dims[i]))
	       {
		  do_index_error (i, indx, at_dims[i]);
		  SLang_free_array (new_at);
		  return -1;
	       }
	     indices[i] = indx;
	  }

	if (-1 == _pSLarray_aget_transfer_elem (at, indices, (VOID_STAR)new_data, sizeof_type, is_ptr))
	  {
	     SLang_free_array (new_at);
	     return -1;
	  }
	new_data += sizeof_type;
	if (num_indices == 1) 
	  {
	     map_indices[0]++;
	     if (map_indices[0] == max_dims[0])
	       break;
	  }
	else if (0 != _pSLarray_next_index (map_indices, max_dims, num_indices))
	  break;	
     }

   if (new_at != NULL)
     {
	int num_dims = 0;
	/* Fixup dimensions on array */
	for (i = 0; i < num_indices; i++)
	  {
	     if (is_dim_array[i]) /* was: (max_dims[i] > 1) */
	       {
		  new_at->dims[num_dims] = max_dims[i];
		  num_dims++;
	       }
	  }

	if (num_dims != 0) new_at->num_dims = num_dims;
	return SLang_push_array (new_at, 1);
     }

   /* Here new_data is a whole new copy, so free it after the push */
   new_data -= sizeof_type;
   if (is_ptr && (*(VOID_STAR *)new_data == NULL))
     ret = SLang_push_null ();
   else
     {
	ret = (*cl->cl_apush) (at->data_type, (VOID_STAR)new_data);
	(*cl->cl_adestroy) (at->data_type, (VOID_STAR)new_data);
     }

   return ret;
}

static int push_string_as_array (unsigned char *s, unsigned int len)
{
   SLindex_Type ilen;
   SLang_Array_Type *at;

   ilen = (SLindex_Type) len;

   at = SLang_create_array (SLANG_UCHAR_TYPE, 0, NULL, &ilen, 1);
   if (at == NULL)
     return -1;

   memcpy ((char *)at->data, (char *)s, len);
   return SLang_push_array (at, 1);
}

static int pop_array_as_string (char **sp)
{
   SLang_Array_Type *at;
   int ret;

   *sp = NULL;

   if (-1 == SLang_pop_array_of_type (&at, SLANG_UCHAR_TYPE))
     return -1;

   ret = 0;

   if (NULL == (*sp = SLang_create_nslstring ((char *) at->data, at->num_elements)))
     ret = -1;

   SLang_free_array (at);
   return ret;
}

static int pop_array_as_bstring (SLang_BString_Type **bs)
{
   SLang_Array_Type *at;
   int ret;

   *bs = NULL;

   if (-1 == SLang_pop_array_of_type (&at, SLANG_UCHAR_TYPE))
     return -1;

   ret = 0;

   if (NULL == (*bs = SLbstring_create ((unsigned char *) at->data, at->num_elements)))
     ret = -1;

   SLang_free_array (at);
   return ret;
}

static int aget_from_array (unsigned int num_indices)
{
   SLang_Array_Type *at;
   SLang_Object_Type index_objs [SLARRAY_MAX_DIMS];
   int ret;
   int is_index_array;
   unsigned int i;

   if (num_indices > SLARRAY_MAX_DIMS)
     {
	SLang_verror (SL_INVALID_PARM, "Number of dims must be less than %d", SLARRAY_MAX_DIMS);
	return -1;
     }

   if (-1 == pop_array (&at, 1))
     return -1;

   if (-1 == pop_indices (at, index_objs, num_indices, &is_index_array))
     {
	SLang_free_array (at);
	return -1;
     }

   if (is_index_array == 0)
     {
#if SLANG_OPTIMIZE_FOR_SPEED
	if ((num_indices == 1) && (index_objs[0].data_type == SLANG_INT_TYPE)
	    && (0 == (at->flags & (SLARR_DATA_VALUE_IS_RANGE|SLARR_DATA_VALUE_IS_POINTER)))
	    && (1 == at->num_dims)
	    && (at->data != NULL))
	  {
	     SLindex_Type ofs = index_objs[0].v.int_val;
	     if (ofs < 0) ofs += at->dims[0];
	     if ((ofs >= at->dims[0]) || (ofs < 0))
	       ret = aget_from_indices (at, index_objs, num_indices);
	     else switch (at->data_type)
	       {
		case SLANG_CHAR_TYPE:
		  ret = SLclass_push_char_obj (SLANG_CHAR_TYPE, *((char *)at->data + ofs));
		  break;
		case SLANG_INT_TYPE:
		  ret = SLclass_push_int_obj (SLANG_INT_TYPE, *((int *)at->data + ofs));
		  break;
#if SLANG_HAS_FLOAT
		case SLANG_DOUBLE_TYPE:
		  ret = SLclass_push_double_obj (SLANG_DOUBLE_TYPE, *((double *)at->data + ofs));
		  break;
#endif
		default:
		  ret = aget_from_indices (at, index_objs, num_indices);
	       }
	  }
	else
#endif
	ret = aget_from_indices (at, index_objs, num_indices);
     }
   else
     ret = aget_from_index_array (at, index_objs[0].v.array_val);

   SLang_free_array (at);
   for (i = 0; i < num_indices; i++)
     SLang_free_object (index_objs + i);

   return ret;
}

static int push_string_element (SLtype type, unsigned char *s, unsigned int len)
{
   int i;

   if (SLang_peek_at_stack () == SLANG_ARRAY_TYPE)
     {
	char *str;

	/* The indices are array values.  So, do this: */
	if (-1 == push_string_as_array (s, len))
	  return -1;

	if (-1 == aget_from_array (1))
	  return -1;

	if (type == SLANG_BSTRING_TYPE)
	  {
	     SLang_BString_Type *bs;
	     int ret;

	     if (-1 == pop_array_as_bstring (&bs))
	       return -1;

	     ret = SLang_push_bstring (bs);
	     SLbstring_free (bs);
	     return ret;
	  }

	if (-1 == pop_array_as_string (&str))
	  return -1;
	return _pSLang_push_slstring (str);   /* frees s upon error */
     }

   if (-1 == SLang_pop_integer (&i))
     return -1;

   if (i < 0) i = i + (int)len;
   if ((unsigned int) i > len)
     i = len;			       /* get \0 character --- bstrings include it as well */

   return SLang_push_uchar (s[(unsigned int)i]);
}

/* ARRAY[i, j, k] generates code: __args i j ...k ARRAY __aput/__aget
 * Here i, j, ... k may be a mixture of integers and 1-d arrays, or
 * a single array of indices.  The index array is generated by the
 * 'where' function.
 *
 * If ARRAY is of type DataType, then this function will create an array of
 * the appropriate type.  In that case, the indices i, j, ..., k must be
 * integers.
 */
int _pSLarray_aget1 (unsigned int num_indices)
{
   int type;
   int (*aget_fun) (SLtype, unsigned int);

   type = SLang_peek_at_stack ();
   switch (type)
     {
      case -1:
	return -1;		       /* stack underflow */

      case SLANG_DATATYPE_TYPE:
	return push_create_new_array (num_indices);

      case SLANG_BSTRING_TYPE:
	if (1 == num_indices)
	  {
	     SLang_BString_Type *bs;
	     int ret;
	     unsigned int len;
	     unsigned char *s;

	     if (-1 == SLang_pop_bstring (&bs))
	       return -1;

	     if (NULL == (s = SLbstring_get_pointer (bs, &len)))
	       ret = -1;
	     else
	       ret = push_string_element (type, s, len);

	     SLbstring_free (bs);
	     return ret;
	  }
	break;

      case SLANG_STRING_TYPE:
	if (1 == num_indices)
	  {
	     char *s;
	     int ret;

	     if (-1 == SLang_pop_slstring (&s))
	       return -1;

	     ret = push_string_element (type, (unsigned char *)s, _pSLstring_bytelen (s));
	     _pSLang_free_slstring (s);
	     return ret;
	  }
	break;

      case SLANG_ARRAY_TYPE:
	break;

      case SLANG_ASSOC_TYPE:
	return _pSLassoc_aget (type, num_indices);

      default:
	aget_fun = _pSLclass_get_class (type)->cl_aget;
	if (NULL != aget_fun)
	  return (*aget_fun) (type, num_indices);
     }

   return aget_from_array (num_indices);
}

int _pSLarray_aget (void)
{
   return _pSLarray_aget1 ((unsigned int)(SLang_Num_Function_Args-1));
}



_INLINE_ int
_pSLarray_aput_transfer_elem (SLang_Array_Type *at, SLindex_Type *indices,
			     VOID_STAR data_to_put, size_t sizeof_type, int is_ptr)
{
   VOID_STAR at_data;

   /* 
    * A range array is not allowed here.  I should add a check for it.  At
    * the moment, one will not get here.
    */
   if (NULL == (at_data = get_data_addr (at, indices)))
     return -1;

   if (is_ptr == 0)
     {
	memcpy ((char *) at_data, (char *)data_to_put, sizeof_type);
	return 0;
     }

   return transfer_n_elements (at, at_data, data_to_put, sizeof_type, 1, is_ptr);
}

static int
aput_get_data_to_put (SLang_Class_Type *cl, unsigned int num_elements, int allow_array,
		       SLang_Array_Type **at_ptr, char **data_to_put, SLuindex_Type *data_increment)
{
   SLtype data_type;
   int type;
   SLang_Array_Type *at;
   
   *at_ptr = NULL;

   data_type = cl->cl_data_type;
   type = SLang_peek_at_stack ();

   if ((SLtype)type != data_type)
     {
	if ((type != SLANG_NULL_TYPE)
	    || ((cl->cl_class_type != SLANG_CLASS_TYPE_PTR)
		&& (cl->cl_class_type != SLANG_CLASS_TYPE_MMT)))
	  {
	     if (-1 == SLclass_typecast (data_type, 1, allow_array))
	       return -1;
	  }
	else
	  {
	     /* This bit of code allows, e.g., a[10] = NULL; */
	     *data_increment = 0;
	     *data_to_put = (char *) cl->cl_transfer_buf;
	     *((char **)cl->cl_transfer_buf) = NULL;
	     return SLdo_pop ();
	  }
     }

   if (allow_array
       && (data_type != SLANG_ARRAY_TYPE)
       && (data_type != SLANG_ANY_TYPE)
       && (SLANG_ARRAY_TYPE == SLang_peek_at_stack ()))
     {
	if (-1 == SLang_pop_array (&at, 0))
	  return -1;

	if ((at->num_elements != num_elements)
#if 0
	    || (at->num_dims != 1)
#endif
	    )
	  {
	     SLang_verror (SL_Index_Error, "Array size is inappropriate for use with index-array");
	     SLang_free_array (at);
	     return -1;
	  }

	*data_to_put = (char *) at->data;
	*data_increment = at->sizeof_type;
	*at_ptr = at;
	return 0;
     }

   *data_increment = 0;
   *data_to_put = (char *) cl->cl_transfer_buf;

   if (-1 == (*cl->cl_apop)(data_type, (VOID_STAR) *data_to_put))
     return -1;

   return 0;
}

static int
aput_from_indices (SLang_Array_Type *at,
		   SLang_Object_Type *index_objs, unsigned int num_indices)
{
   SLindex_Type *index_data [SLARRAY_MAX_DIMS];
   SLindex_Type range_buf [SLARRAY_MAX_DIMS];
   SLindex_Type range_delta_buf [SLARRAY_MAX_DIMS];
   SLindex_Type max_dims [SLARRAY_MAX_DIMS];
   SLindex_Type *at_dims;
   unsigned int i, num_elements;
   SLang_Array_Type *bt;
   SLindex_Type map_indices[SLARRAY_MAX_DIMS];
   SLindex_Type indices [SLARRAY_MAX_DIMS];
   size_t sizeof_type;
   int is_ptr, is_array, ret;
   char *data_to_put;
   SLuindex_Type data_increment;
   SLang_Class_Type *cl;
   int is_dim_array [SLARRAY_MAX_DIMS];

   if (-1 == convert_nasty_index_objs (at, index_objs, num_indices,
				       index_data, range_buf, range_delta_buf,
				       max_dims, &num_elements, &is_array,
				       is_dim_array))
     return -1;

   cl = at->cl;

   if (-1 == aput_get_data_to_put (cl, num_elements, is_array,
				    &bt, &data_to_put, &data_increment))
     return -1;

   sizeof_type = at->sizeof_type;
   is_ptr = (at->flags & SLARR_DATA_VALUE_IS_POINTER);

   ret = -1;

   at_dims = at->dims;
   SLMEMSET((char *) map_indices, 0, sizeof(map_indices));
   if (num_elements) while (1)
     {
	for (i = 0; i < num_indices; i++)
	  {
	     SLindex_Type j = map_indices[i];
	     SLindex_Type indx;

	     if (0 != range_delta_buf[i])
	       indx = range_buf[i] + j * range_delta_buf[i];
	     else
	       indx = index_data [i][j];

	     if (indx < 0)
	       indx += at_dims[i];

	     if ((indx < 0) || (indx >= at_dims[i]))
	       {
		  do_index_error (i, indx, at_dims[i]);
		  goto return_error;
	       }
	     indices[i] = indx;
	  }

	if (-1 == _pSLarray_aput_transfer_elem (at, indices, (VOID_STAR)data_to_put, sizeof_type, is_ptr))
	  goto return_error;

	data_to_put += data_increment;
	if (num_indices == 1) 
	  {
	     map_indices[0]++;
	     if (map_indices[0] == max_dims[0])
	       break;
	  }
	else if (0 != _pSLarray_next_index (map_indices, max_dims, num_indices))
	  break;
     }

   ret = 0;

   /* drop */

   return_error:
   if (bt == NULL)
     {
	if (is_ptr)
	  (*cl->cl_destroy) (cl->cl_data_type, (VOID_STAR) data_to_put);
     }
   else SLang_free_array (bt);

   return ret;
}

#if SLANG_OPTIMIZE_FOR_SPEED
static int aput_doubles_from_index_array (char *data_to_put, SLuindex_Type data_increment,
					  SLindex_Type *indices, SLindex_Type *indices_max,
					  double *dest_data, SLindex_Type num_elements)
{
   while (indices < indices_max)
     {
	SLindex_Type i = *indices;
	     
	if (i < 0) 
	  {
	     i += num_elements;
	     if (i < 0)
	       i = num_elements;
	  }
	if (i >= num_elements)
	  {
	     SLang_set_error (SL_Index_Error);
	     return -1;
	  }
	dest_data[i] = *(double *)data_to_put;
	indices++;
	data_to_put += data_increment;
     }
   return 0;
}
static int aput_floats_from_index_array (char *data_to_put, SLuindex_Type data_increment,
					 SLindex_Type *indices, SLindex_Type *indices_max,
					 float *dest_data, SLindex_Type num_elements)
{
   while (indices < indices_max)
     {
	SLindex_Type i = *indices;
	     
	if (i < 0) 
	  {
	     i += num_elements;
	     if (i < 0)
	       i = num_elements;
	  }
	if (i >= num_elements)
	  {
	     SLang_set_error (SL_Index_Error);
	     return -1;
	  }
	dest_data[i] = *(float *)data_to_put;
	indices++;
	data_to_put += data_increment;
     }
   return 0;
}
static int aput_ints_from_index_array (char *data_to_put, SLuindex_Type data_increment,
				       SLindex_Type *indices, SLindex_Type *indices_max,
				       int *dest_data, SLindex_Type num_elements)
{
   while (indices < indices_max)
     {
	SLindex_Type i = *indices;
	     
	if (i < 0) 
	  {
	     i += num_elements;
	     if (i < 0)
	       i = num_elements;
	  }
	if (i >= num_elements)
	  {
	     SLang_set_error (SL_Index_Error);
	     return -1;
	  }
	dest_data[i] = *(int *)data_to_put;
	indices++;
	data_to_put += data_increment;
     }
   return 0;
}
#endif

static int
aput_from_index_array (SLang_Array_Type *at, SLang_Array_Type *ind_at)
{
   SLindex_Type *indices, *indices_max;
   size_t sizeof_type;
   char *data_to_put, *dest_data;
   SLuindex_Type data_increment;
   SLindex_Type num_elements;
   int is_ptr;
   SLang_Array_Type *bt;
   SLang_Class_Type *cl;
   int ret;

   if (-1 == coerse_array_to_linear (at))
     return -1;

   if (-1 == coerse_array_to_linear (ind_at))
     return -1;

   sizeof_type = at->sizeof_type;

   cl = at->cl;

   /* Note that if bt is returned as non NULL, then the array is a linear
    * one.
    */
   if (-1 == aput_get_data_to_put (cl, ind_at->num_elements, 1,
				    &bt, &data_to_put, &data_increment))
     return -1;

   /* Since the index array is linear, I can address it directly */
   indices = (SLindex_Type *) ind_at->data;
   indices_max = indices + ind_at->num_elements;

   is_ptr = (at->flags & SLARR_DATA_VALUE_IS_POINTER);
   dest_data = (char *) at->data;
   num_elements = (SLindex_Type) at->num_elements;

   ret = -1;
   switch (at->data_type)
     {
#if SLANG_OPTIMIZE_FOR_SPEED
# if SLANG_HAS_FLOAT
      case SLANG_DOUBLE_TYPE:
	if (-1 == aput_doubles_from_index_array (data_to_put, data_increment,
						 indices, indices_max, 
						 (double*)dest_data, num_elements))
	  goto return_error;
	break;

      case SLANG_FLOAT_TYPE:
	if (-1 == aput_floats_from_index_array (data_to_put, data_increment,
						indices, indices_max, 
						(float*)dest_data, num_elements))
	  goto return_error;
	break;
# endif
      case SLANG_INT_TYPE:
	if (-1 == aput_ints_from_index_array (data_to_put, data_increment,
					      indices, indices_max, 
					      (int*)dest_data, num_elements))
	  goto return_error;
	break;

#endif 
      default:
	while (indices < indices_max)
	  {
	     size_t offset;
	     SLindex_Type i = *indices;
	     
	     if (i < 0) 
	       {
		  i += num_elements;
		  if (i < 0)
		    i = num_elements;
	       }
	     if (i >= num_elements)
	       {
		  SLang_set_error (SL_Index_Error);
		  goto return_error;
	       }

	     offset = sizeof_type * (SLuindex_Type)i;
	     
	     if (-1 == transfer_n_elements (at, (VOID_STAR) (dest_data + offset),
					    (VOID_STAR) data_to_put, sizeof_type, 1,
					    is_ptr))
	       goto return_error;
	     
	     indices++;
	     data_to_put += data_increment;
	  }
     }
   
   ret = 0;
   /* Drop */

   return_error:

   if (bt == NULL)
     {
	if (is_ptr)
	  (*cl->cl_destroy) (cl->cl_data_type, (VOID_STAR)data_to_put);
     }
   else SLang_free_array (bt);

   return ret;
}

/* ARRAY[i, j, k] = generates code: __args i j k ARRAY __aput
 */
int _pSLarray_aput1 (unsigned int num_indices)
{
   SLang_Array_Type *at;
   SLang_Object_Type index_objs [SLARRAY_MAX_DIMS];
   int ret;
   int is_index_array;
   int (*aput_fun) (SLtype, unsigned int);
   int type;

   ret = -1;

   type = SLang_peek_at_stack ();
   switch (type)
     {
      case -1:
	return -1;

      case SLANG_ARRAY_TYPE:
	break;

      case SLANG_ASSOC_TYPE:
	return _pSLassoc_aput (type, num_indices);

      default:
	if (NULL != (aput_fun = _pSLclass_get_class (type)->cl_aput))
	  return (*aput_fun) (type, num_indices);
	break;
     }

   if (-1 == SLang_pop_array (&at, 0))
     return -1;

   if (at->flags & SLARR_DATA_VALUE_IS_READ_ONLY)
     {
	SLang_verror (SL_READONLY_ERROR, "%s Array is read-only",
		      SLclass_get_datatype_name (at->data_type));
	SLang_free_array (at);
	return -1;
     }

   if (-1 == pop_indices (at, index_objs, num_indices, &is_index_array))
     {
	SLang_free_array (at);
	return -1;
     }

   if (is_index_array == 0)
     {
#if SLANG_OPTIMIZE_FOR_SPEED
	if ((num_indices == 1) && (index_objs[0].data_type == SLANG_INT_TYPE)
	    && (0 == (at->flags & (SLARR_DATA_VALUE_IS_RANGE|SLARR_DATA_VALUE_IS_POINTER)))
	    && (1 == at->num_dims)
	    && (at->data != NULL))
	  {
	     SLindex_Type ofs = index_objs[0].v.int_val;
	     if (ofs < 0) ofs += at->dims[0];
	     if ((ofs >= at->dims[0]) || (ofs < 0))
	       ret = aput_from_indices (at, index_objs, num_indices);
	     else switch (at->data_type)
	       {
		case SLANG_CHAR_TYPE:
		  ret = SLang_pop_char (((char *)at->data + ofs));
		  break;
		case SLANG_INT_TYPE:
		  ret = SLang_pop_integer (((int *)at->data + ofs));
		  break;
#if SLANG_HAS_FLOAT
		case SLANG_DOUBLE_TYPE:
		  ret = SLang_pop_double ((double *)at->data + ofs);
		  break;
#endif
		default:
		  ret = aput_from_indices (at, index_objs, num_indices);
	       }
	     SLang_free_array (at);
	     return ret;
	  }
#endif
	ret = aput_from_indices (at, index_objs, num_indices);
     }
   else
     ret = aput_from_index_array (at, index_objs[0].v.array_val);

   SLang_free_array (at);
   free_index_objects (index_objs, num_indices);
   return ret;
}

int _pSLarray_aput (void)
{
   return _pSLarray_aput1 ((unsigned int)(SLang_Num_Function_Args-1));
}


/* This is for 1-d matrices only.  It is used by the sort function */
static int push_element_at_index (SLang_Array_Type *at, SLindex_Type indx)
{
   VOID_STAR data;

   if (NULL == (data = get_data_addr (at, &indx)))
     return -1;

   return push_element_at_addr (at, (VOID_STAR) data, 1);
}

static SLang_Name_Type *Sort_Function;
static SLang_Array_Type *Sort_Array;

#if SLANG_OPTIMIZE_FOR_SPEED
static int double_sort_fun (SLindex_Type *a, SLindex_Type *b)
{
   double *da, *db;
   
   da = (double *) Sort_Array->data;
   db = da + *b;
   da = da + *a;

   if (*da > *db) return 1;
   if (*da == *db) return 0;
   return -1;
}
static int int_sort_fun (SLindex_Type *a, SLindex_Type *b)
{
   int *da, *db;
   
   da = (int *) Sort_Array->data;
   db = da + *b;
   da = da + *a;

   if (*da > *db) return 1;
   if (*da == *db) return 0;
   return -1;
}
#endif

static int sort_cmp_fun (SLindex_Type *a, SLindex_Type *b)
{
   int cmp;

   if (SLang_get_error ()
       || (-1 == push_element_at_index (Sort_Array, *a))
       || (-1 == push_element_at_index (Sort_Array, *b))
       || (-1 == SLexecute_function (Sort_Function))
       || (-1 == SLang_pop_integer (&cmp)))
     {
	/* DO not allow qsort to loop forever.  Return something meaningful */
	if (*a > *b) return 1;
	if (*a < *b) return -1;
	return 0;
     }

   return cmp;
}

static int builtin_sort_cmp_fun (SLindex_Type *a, SLindex_Type *b)
{
   VOID_STAR a_data;
   VOID_STAR b_data;
   SLang_Class_Type *cl;
   
   cl = Sort_Array->cl;

   if ((SLang_get_error () == 0)
       && (NULL != (a_data = get_data_addr (Sort_Array, a)))
       && (NULL != (b_data = get_data_addr (Sort_Array, b))))
     {
	int cmp;

	if ((Sort_Array->flags & SLARR_DATA_VALUE_IS_POINTER)
	    && ((*(VOID_STAR *) a_data == NULL) || (*(VOID_STAR *) a_data == NULL)))
	  {
	     SLang_verror (SL_VARIABLE_UNINITIALIZED,
			   "%s array has uninitialized element", cl->cl_name);
	  }
	else if (0 == (*cl->cl_cmp)(Sort_Array->data_type, a_data, b_data, &cmp))
	  return cmp;
     }
       
       
   if (*a > *b) return 1;
   if (*a == *b) return 0;
   return -1;
}

static void sort_array_internal (SLang_Array_Type *at_str, 
				 SLang_Name_Type *entry,
				 int (*sort_fun)(SLindex_Type *, SLindex_Type *))
{
   SLang_Array_Type *ind_at;
   /* This is a silly hack to make up for braindead compilers and the lack of
    * uniformity in prototypes for qsort.
    */
   void (*qsort_fun) (char *, unsigned int, int, int (*)(SLindex_Type *, SLindex_Type *));
   SLindex_Type *indx;
   int i, n;
   SLindex_Type dims[1];

   if (Sort_Array != NULL)
     {
	SLang_verror (SL_NOT_IMPLEMENTED, "array_sort is not recursive");
	return;
     }

   n = at_str->num_elements;

   if (at_str->num_dims != 1)
     {
	SLang_verror (SL_INVALID_PARM, "sort is restricted to 1 dim arrays");
	return;
     }

   dims [0] = n;

   if (NULL == (ind_at = SLang_create_array (SLANG_ARRAY_INDEX_TYPE, 0, NULL, dims, 1)))
     return;

   indx = (SLindex_Type *) ind_at->data;
   for (i = 0; i < n; i++) indx[i] = i;

   if (n > 1)
     {
	qsort_fun = (void (*)(char *, unsigned int, int, int (*)(SLindex_Type *,
								 SLindex_Type *)))
	  qsort;

	Sort_Array = at_str;
	Sort_Function = entry;
	(*qsort_fun) ((char *) indx, n, sizeof (SLindex_Type), sort_fun);
     }

   Sort_Array = NULL;
   (void) SLang_push_array (ind_at, 1);
}

static void sort_array (void)
{
   SLang_Name_Type *entry;
   SLang_Array_Type *at;
   int (*sort_fun) (SLindex_Type *, SLindex_Type *);

   if (SLang_Num_Function_Args != 1)
     {
	sort_fun = sort_cmp_fun;

	if (NULL == (entry = SLang_pop_function ()))
	  return;

	if (-1 == SLang_pop_array (&at, 1))
	  return;
     }
   else
     {
	if (-1 == SLang_pop_array (&at, 1))
	  return;
	
#if SLANG_OPTIMIZE_FOR_SPEED
#if SLANG_HAS_FLOAT
	if (at->data_type == SLANG_DOUBLE_TYPE)
	  sort_fun = double_sort_fun;
	else
#endif
	  if (at->data_type == SLANG_INT_TYPE)
	  sort_fun = int_sort_fun;
	else
#endif
	  sort_fun = builtin_sort_cmp_fun;

	if (at->cl->cl_cmp == NULL)
	  {
	     SLang_verror (SL_NOT_IMPLEMENTED, 
			   "%s does not have a predefined sorting method",
			   at->cl->cl_name);
	     SLang_free_array (at);
	     return;
	  }
	entry = NULL;
     }

   sort_array_internal (at, entry, sort_fun);
   SLang_free_array (at);
   SLang_free_function (entry);
}

static void bstring_to_array (SLang_BString_Type *bs)
{
   unsigned char *s;
   unsigned int len;
   
   if (NULL == (s = SLbstring_get_pointer (bs, &len)))
     (void) SLang_push_null ();
   else
     (void) push_string_as_array (s, len);
}

static void array_to_bstring (SLang_Array_Type *at)
{
   size_t nbytes;
   SLang_BString_Type *bs;

   nbytes = at->num_elements * at->sizeof_type;
   bs = SLbstring_create ((unsigned char *)at->data, nbytes);
   (void) SLang_push_bstring (bs);
   SLbstring_free (bs);
}

static void init_char_array (void)
{
   SLang_Array_Type *at;
   char *s;
   unsigned int n, ndim;

   if (SLang_pop_slstring (&s)) return;

   if (-1 == SLang_pop_array (&at, 0))
     goto free_and_return;

   if (at->data_type != SLANG_CHAR_TYPE)
     {
	SLang_verror (SL_TYPE_MISMATCH, "Operation requires a character array");
	goto free_and_return;
     }

   n = _pSLstring_bytelen (s);
   ndim = at->num_elements;
   if (n > ndim)
     {
	SLang_verror (SL_INVALID_PARM, "String too big to initialize array");
	goto free_and_return;
     }

   strncpy((char *) at->data, s, ndim);
   /* drop */

   free_and_return:
   SLang_free_array (at);
   _pSLang_free_slstring (s);
}


static VOID_STAR range_get_data_addr (SLang_Array_Type *at, SLindex_Type *dims)
{
   static int value;
   SLarray_Range_Array_Type *r;
   SLindex_Type d;

   d = *dims;
   r = (SLarray_Range_Array_Type *)at->data;

   if (d < 0)
     d += at->dims[0];

   if ((SLuindex_Type)d >= at->num_elements)
     {
	SLang_set_error (SL_Index_Error);
	return NULL;
     }
   value = r->first_index + d * r->delta;
   return (VOID_STAR) &value;
}

static SLang_Array_Type 
  *create_range_array (SLarray_Range_Array_Type *range, SLindex_Type num,
		       SLtype type, int (*to_linear_fun) (SLang_Array_Type *, SLarray_Range_Array_Type *, VOID_STAR))
{
   SLarray_Range_Array_Type *r;
   SLang_Array_Type *at;

   r = (SLarray_Range_Array_Type *) SLmalloc (sizeof (SLarray_Range_Array_Type));
   if (r == NULL)
     return NULL;
   memset((char *) r, 0, sizeof (SLarray_Range_Array_Type));

   if (NULL == (at = SLang_create_array (type, 0, (VOID_STAR) range, &num, 1)))
     {
	SLfree ((char *)range);
	return NULL;
     }
   r->first_index = range->first_index;
   r->last_index = range->last_index;
   r->delta = range->delta;
   r->has_first_index = range->has_first_index;
   r->has_last_index = range->has_last_index;
   r->to_linear_fun = to_linear_fun;
   at->data = (VOID_STAR) r;
   at->index_fun = range_get_data_addr;
   at->flags |= SLARR_DATA_VALUE_IS_RANGE;
   return at;
}

static int get_range_array_limits (SLindex_Type *first_indexp, SLindex_Type *last_indexp, SLindex_Type *deltap,
				   SLarray_Range_Array_Type *r, SLindex_Type *nump)
{
   SLindex_Type first_index, last_index, delta;
   SLindex_Type num;

   if (deltap == NULL) delta = 1;
   else delta = *deltap;

   if (delta == 0)
     {
	SLang_verror (SL_INVALID_PARM, "range-array increment must be non-zero");
	return -1;
     }

   r->has_first_index = (first_indexp != NULL);
   if (r->has_first_index)
     first_index = *first_indexp;
   else
     first_index = 0;

   r->has_last_index = (last_indexp != NULL);
   if (r->has_last_index)
     last_index = *last_indexp;
   else
     last_index = -1;

   num = 0;
   if (delta > 0)
     {
	/* Case: [20:10:11] --> 0 elements, [10:20:11] --> 1 element */
	if (last_index >= first_index)
	  num = 1 + (last_index - first_index) / delta;
     }
   else 
     {
	/* Case: [20:10:-11] -> 1 element, [20:30:-11] -> none */
	if (last_index <= first_index)
	  num = 1 + (last_index - first_index) / delta;
     }
   
   r->first_index = first_index;
   r->last_index = last_index;
   r->delta = delta;
   *nump = num;
   
   return 0;
}

static int index_range_to_linear (SLang_Array_Type *at, SLarray_Range_Array_Type *range, VOID_STAR buf)
{
   SLindex_Type *data = (SLindex_Type *)buf;
   SLindex_Type i, imax;
   SLindex_Type xmin, dx;

   imax = (SLindex_Type) at->num_elements;
   xmin = range->first_index;
   dx = range->delta;
   for (i = 0; i < imax; i++)
     {
	data [i] = xmin;
	xmin += dx;
     }
   return 0;
}

static int int_range_to_linear (SLang_Array_Type *at, SLarray_Range_Array_Type *range, VOID_STAR buf)
{
   int *data = (int *)buf;
   unsigned int i, imax;
   int xmin, dx;

   imax = (unsigned int)at->num_elements;
   xmin = (int) range->first_index;
   dx = (int) range->delta;
   for (i = 0; i < imax; i++)
     {
	data [i] = xmin;
	xmin += dx;
     }
   return 0;
}

static SLang_Array_Type *inline_implicit_int_array (SLindex_Type *xminptr, SLindex_Type *xmaxptr, SLindex_Type *dxptr)
{
   SLarray_Range_Array_Type r;
   SLindex_Type num;

   if (-1 == get_range_array_limits (xminptr, xmaxptr, dxptr, &r, &num))
     return NULL;

   return create_range_array (&r, num, SLANG_INT_TYPE, int_range_to_linear);
}

#if SLANG_HAS_FLOAT
static SLang_Array_Type *inline_implicit_floating_array (SLtype type,
							 double *xminptr, double *xmaxptr, double *dxptr)
{
   SLindex_Type n, i;
   SLang_Array_Type *at;
   SLindex_Type dims;
   double xmin, xmax, dx;

   if ((xminptr == NULL) || (xmaxptr == NULL))
     {
	SLang_verror (SL_INVALID_PARM, "range-array has unknown size");
	return NULL;
     }
   xmin = *xminptr;
   xmax = *xmaxptr;
   if (dxptr == NULL) dx = 1.0;
   else dx = *dxptr;

   if (dx == 0.0)
     {
	SLang_verror (SL_INVALID_PARM, "range-array increment must be non-zero");
	return NULL;
     }

   /* I have convinced myself that it is better to use semi-open intervals
    * because of less ambiguities.  So, [a:b:c] will represent the set of
    * values a, a + c, a + 2c ... a + nc
    * such that a + nc < b.  That is, b lies outside the interval.
    */

   /* Allow for roundoff by adding 0.5 before truncation */
   n = (int)(1.5 + ((xmax - xmin) / dx));
   if (n <= 0)
     n = 0;
   else
     {
	double last = xmin + (n-1) * dx;

	if (dx > 0.0)
	  {
	     if (last >= xmax)
	       n -= 1;
	  }
	else if (last <= xmax)
	  n -= 1;
     }
   /* FIXME: Priority=medium: Add something to detect cases where the increment is too small */

   dims = n;
   if (NULL == (at = SLang_create_array1 (type, 0, NULL, &dims, 1, 1)))
     return NULL;

   if (type == SLANG_DOUBLE_TYPE)
     {
	double *ptr;

	ptr = (double *) at->data;

	for (i = 0; i < n; i++)
	  ptr[i] = xmin + i * dx;
     }
   else
     {
	float *ptr;

	ptr = (float *) at->data;

	for (i = 0; i < n; i++)
	  ptr[i] = (float) (xmin + i * dx);
     }
   return at;
}
#endif

/* FIXME: Priority=medium
 * This needs to be updated to work with all integer types.
 * Adding support for other types is going to require a generalization
 * of the Range_Array_Type object.
 */
int _pSLarray_inline_implicit_array (void)
{
   SLindex_Type index_vals[3];
#if SLANG_HAS_FLOAT
   double double_vals[3];
   int is_int;
#endif
   int has_vals[3];
   unsigned int i, count;
   SLang_Array_Type *at;
   int precedence;
   SLtype type;

   count = SLang_Num_Function_Args;

   if (count == 2)
     has_vals [2] = 0;
   else if (count != 3)
     {
	SLang_verror (SL_NUM_ARGS_ERROR, "wrong number of arguments to __implicit_inline_array");
	return -1;
     }

#if SLANG_HAS_FLOAT
   is_int = 1;
#endif

   type = 0;
   precedence = 0;

   i = count;
   while (i--)
     {
	int this_type, this_precedence;
	int itmp;

	if (-1 == (this_type = SLang_peek_at_stack ()))
	  return -1;

	this_precedence = _pSLarith_get_precedence ((SLtype) this_type);
	if (precedence < this_precedence)
	  {
	     type = (SLtype) this_type;
	     precedence = this_precedence;
	  }

	has_vals [i] = 1;

	switch (this_type)
	  {
	   case SLANG_NULL_TYPE:
	     has_vals[i] = 0;
	     (void) SLdo_pop ();
	     break;

#if SLANG_HAS_FLOAT
	   case SLANG_DOUBLE_TYPE:
	   case SLANG_FLOAT_TYPE:
	     if (-1 == SLang_pop_double (double_vals + i))
	       return -1;
	     is_int = 0;
	     break;
#endif
	   default:
	     if (-1 == SLang_pop_integer (&itmp))
	       return -1;
	     index_vals[i] = itmp;
#if SLANG_HAS_FLOAT
	     double_vals[i] = (double) itmp;
#endif
	  }
     }

#if SLANG_HAS_FLOAT
   if (is_int == 0)
     at = inline_implicit_floating_array (type,
					  (has_vals[0] ? &double_vals[0] : NULL),
					  (has_vals[1] ? &double_vals[1] : NULL),
					  (has_vals[2] ? &double_vals[2] : NULL));
   else
#endif
     at = inline_implicit_int_array ((has_vals[0] ? &index_vals[0] : NULL),
				     (has_vals[1] ? &index_vals[1] : NULL),
				     (has_vals[2] ? &index_vals[2] : NULL));

   if (at == NULL)
     return -1;

   return SLang_push_array (at, 1);
}

static int try_typecast_range_array (SLang_Array_Type *at, SLtype to_type, 
				     SLang_Array_Type **btp)
{
   SLang_Array_Type *bt;
   
   *btp = NULL;
   if (to_type == SLANG_ARRAY_INDEX_TYPE)
     {
	if (at->data_type == SLANG_INT_TYPE)
	  {
	     SLarray_Range_Array_Type *range;
	     
	     range = (SLarray_Range_Array_Type *)at->data;
	     bt = create_range_array (range, at->num_elements,
				      to_type, index_range_to_linear);
	     if (bt == NULL)
	       return -1;
	     *btp = bt;
	     return 1;
	  }
     }
   return 0;
}


int _pSLarray_wildcard_array (void)
{
   SLang_Array_Type *at;
     
   if (NULL == (at = inline_implicit_int_array (NULL, NULL, NULL)))
     return -1;

   return SLang_push_array (at, 1);
}

/* FIXME: The type-promotion routine needs to be made more generic and
 * better support user-defined types.  
 */

/* Test if the type cannot be promoted further */
_INLINE_ static int nowhere_to_promote (SLtype type)
{
   switch (type)
     {
      case SLANG_COMPLEX_TYPE:
      case SLANG_BSTRING_TYPE:
      case SLANG_ARRAY_TYPE:
	return 1;
     }
   
   return 0;
}

static int promote_to_common_type (SLtype a, SLtype b, SLtype *c)
{
   if (a == b)
     {
	*c = a;
	return 0;
     }
   if (nowhere_to_promote (a))
     {
	/* a type can always be converted to an array: T -> [T] */
	if (b == SLANG_ARRAY_TYPE)
	  *c = b;
	else
	  *c = a;
	return 0;
     }
   if (nowhere_to_promote (b))
     {
	*c = b;
	return 0;
     }

   if (_pSLang_is_arith_type (a) && _pSLang_is_arith_type (b))
     {
	if (_pSLarith_get_precedence (a) > _pSLarith_get_precedence (b))
	  *c = a;
	else
	  *c = b;
	return 0;
     }
   
   if (a == SLANG_NULL_TYPE)
     {
	*c = b;
	return 0;
     }
   if (b == SLANG_NULL_TYPE)
     {
	*c = a;
	return 0;
     }
   
   *c = a;
   return 0;
}

static SLtype get_type_for_concat (SLang_Array_Type **arrays, unsigned int n)
{
   SLtype type;
   unsigned int i;

   type = arrays[0]->data_type;

   for (i = 1; i < n; i++)
     {
	SLtype this_type = arrays[i]->data_type;

	if (this_type == type)
	  continue;

	if (-1 == promote_to_common_type (type, this_type, &type))
	  return SLANG_UNDEFINED_TYPE;
     }
   return type;
}
	
static SLang_Array_Type *concat_arrays (unsigned int count)
{
   SLang_Array_Type **arrays;
   SLang_Array_Type *at, *bt;
   unsigned int i;
   SLindex_Type num_elements;
   SLtype type;
   char *src_data, *dest_data;
   int is_ptr;
   size_t sizeof_type;
   int max_dims, min_dims, max_rows, min_rows;

   arrays = (SLang_Array_Type **)SLmalloc (count * sizeof (SLang_Array_Type *));
   if (arrays == NULL)
     {
	SLdo_pop_n (count);
	return NULL;
     }
   SLMEMSET((char *) arrays, 0, count * sizeof(SLang_Array_Type *));

   at = NULL;

   num_elements = 0;
   i = count;

   while (i != 0)
     {
	i--;

	if (-1 == SLang_pop_array (&bt, 1))   /* bt is now linear */
	  goto free_and_return;

	arrays[i] = bt;
	num_elements += (int)bt->num_elements;
     }

   /* From here on, arrays[*] are linear */

   /* type = arrays[0]->data_type; */
   type = get_type_for_concat (arrays, count);

   max_dims = min_dims = arrays[0]->num_dims;
   min_rows = max_rows = arrays[0]->dims[0];

   for (i = 0; i < count; i++)
     {
	SLang_Array_Type *ct;
	int num;

	bt = arrays[i];

	num = bt->num_dims;
	if (num > max_dims) max_dims = num;
	if (num < min_dims) min_dims = num;

	num = bt->dims[0];
	if (num > max_rows) max_rows = num;
	if (num < min_rows) min_rows = num;

	if (type == bt->data_type)
	  continue;

	if (1 != _pSLarray_typecast (bt->data_type, (VOID_STAR) &bt, 1,
				    type, (VOID_STAR) &ct, 1))
	  goto free_and_return;

	SLang_free_array (bt);
	arrays [i] = ct;
     }

   if (NULL == (at = SLang_create_array (type, 0, NULL, &num_elements, 1)))
     goto free_and_return;

   is_ptr = (at->flags & SLARR_DATA_VALUE_IS_POINTER);
   sizeof_type = at->sizeof_type;
   dest_data = (char *) at->data;

   for (i = 0; i < count; i++)
     {
	bt = arrays[i];

	src_data = (char *) bt->data;
	num_elements = bt->num_elements;

	if (-1 == transfer_n_elements (bt, (VOID_STAR)dest_data, (VOID_STAR)src_data, sizeof_type,
				       num_elements, is_ptr))
	  {
	     SLang_free_array (at);
	     at = NULL;
	     goto free_and_return;
	  }

	dest_data += num_elements * sizeof_type;
     }

#if 0   
   /* If the arrays are all 1-d, and all the same size, then reshape to a
    * 2-d array.  This will allow us to do, e.g.
    * a = [[1,2], [3,4]]
    * to specifiy a 2-d.
    * Someday I will generalize this.
    */
   /* This is a bad idea.  Everyone using it expects concatenation to happen.
    * Perhaps I will extend the syntax to allow a 2-d array to be expressed
    * as [[1,2];[3,4]].
    */
   if ((max_dims == min_dims) && (max_dims == 1) && (min_rows == max_rows))
     {
	at->num_dims = 2;
	at->dims[0] = count;
	at->dims[1] = min_rows;
     }
#endif
   free_and_return:

   for (i = 0; i < count; i++)
     SLang_free_array (arrays[i]);
   SLfree ((char *) arrays);

   return at;
}

int _pSLarray_inline_array (void)
{
   SLang_Object_Type *obj, *objmin;
   SLtype type, this_type;
   unsigned int count;
   SLang_Array_Type *at;

   obj = _pSLang_get_run_stack_pointer ();
   objmin = _pSLang_get_run_stack_base ();

   count = SLang_Num_Function_Args;
   type = 0;

   while ((count > 0) && (--obj >= objmin))
     {
	this_type = obj->data_type;

	if (type == 0)
	  type = this_type;
	else if (type != this_type)
	  {
	     if (-1 == promote_to_common_type (type, this_type, &type))
	       {
		  _pSLclass_type_mismatch_error (type, this_type);
		  return -1;
	       }
	  }
	count--;
     }

   if (count != 0)
     {
	SLang_set_error (SL_STACK_UNDERFLOW);
	return -1;
     }

   count = SLang_Num_Function_Args;

   if (count == 0)
     {
	SLang_verror (SL_NOT_IMPLEMENTED, "Empty inline-arrays not supported");
	return -1;
     }

   if (type == SLANG_ARRAY_TYPE)
     {
	if (count == 1)
	  return 0;		       /* no point in going on */

	if (NULL == (at = concat_arrays (count)))
	  return -1;
     }
   else
     {
	SLang_Object_Type index_obj;
	SLindex_Type icount = (SLindex_Type) count;

	if (NULL == (at = SLang_create_array (type, 0, NULL, &icount, 1)))
	  return -1;

	index_obj.data_type = SLANG_INT_TYPE;
	while (count != 0)
	  {
	     count--;
	     index_obj.v.int_val = (int) count;
	     if (-1 == aput_from_indices (at, &index_obj, 1))
	       {
		  SLang_free_array (at);
		  SLdo_pop_n (count);
		  return -1;
	       }
	  }
     }

   return SLang_push_array (at, 1);
}

static int array_binary_op_result (int op, SLtype a, SLtype b,
				   SLtype *c)
{
   (void) op;
   (void) a;
   (void) b;
   *c = SLANG_ARRAY_TYPE;
   return 1;
}

static int array_binary_op (int op,
			    SLtype a_type, VOID_STAR ap, unsigned int na,
			    SLtype b_type, VOID_STAR bp, unsigned int nb,
			    VOID_STAR cp)
{
   SLang_Array_Type *at, *bt, *ct;
   unsigned int i, num_dims;
   int (*binary_fun) (int,
		      SLtype, VOID_STAR, unsigned int,
		      SLtype, VOID_STAR, unsigned int,
		      VOID_STAR);
   SLang_Class_Type *a_cl, *b_cl, *c_cl;
   int no_init;

   if (a_type == SLANG_ARRAY_TYPE)
     {
	if (na != 1)
	  {
	     SLang_verror (SL_NOT_IMPLEMENTED, "Binary operation on multiple arrays not implemented");
	     return -1;
	  }

	at = *(SLang_Array_Type **) ap;
	if (-1 == coerse_array_to_linear (at))
	  return -1;
	ap = at->data;
	a_type = at->data_type;
	na = at->num_elements;
     }
   else
     {
	at = NULL;
     }

   if (b_type == SLANG_ARRAY_TYPE)
     {
	if (nb != 1)
	  {
	     SLang_verror (SL_NOT_IMPLEMENTED, "Binary operation on multiple arrays not implemented");
	     return -1;
	  }

	bt = *(SLang_Array_Type **) bp;
	if (-1 == coerse_array_to_linear (bt))
	  return -1;
	bp = bt->data;
	b_type = bt->data_type;
	nb = bt->num_elements;
     }
   else
     {
	bt = NULL;
     }

   if ((at != NULL) && (bt != NULL))
     {
	num_dims = at->num_dims;

	if (num_dims != bt->num_dims)
	  {
	     SLang_verror (SL_TYPE_MISMATCH, "Arrays must have same dim for binary operation");
	     return -1;
	  }

	for (i = 0; i < num_dims; i++)
	  {
	     if (at->dims[i] != bt->dims[i])
	       {
		  SLang_verror (SL_TYPE_MISMATCH, "Arrays must be the same for binary operation");
		  return -1;
	       }
	  }
     }

   a_cl = _pSLclass_get_class (a_type);
   b_cl = _pSLclass_get_class (b_type);

   if (NULL == (binary_fun = _pSLclass_get_binary_fun (op, a_cl, b_cl, &c_cl, 1)))
     return -1;

   no_init = ((c_cl->cl_class_type == SLANG_CLASS_TYPE_SCALAR)
	      || (c_cl->cl_class_type == SLANG_CLASS_TYPE_VECTOR));

   ct = NULL;
#if SLANG_USE_TMP_OPTIMIZATION
   /* If we are dealing with scalar (or vector) objects, and if the object
    * appears to be owned by the stack, then use it instead of creating a 
    * new version.  This can happen with code such as:
    * @  x = [1,2,3,4];
    * @  x = __tmp(x) + 1;
    */
   if (no_init)
     {
	if ((at != NULL) 
	    && (at->num_refs == 1)
	    && (at->data_type == c_cl->cl_data_type))
	  {
	     ct = at;
	     ct->num_refs = 2;
	  }
	else if ((bt != NULL) 
	    && (bt->num_refs == 1)
	    && (bt->data_type == c_cl->cl_data_type))
	  {
	     ct = bt;
	     ct->num_refs = 2;
	  }
     }
#endif				       /* SLANG_USE_TMP_OPTIMIZATION */
   
   if (ct == NULL)
     {
	if (at != NULL) ct = at; else ct = bt;
	ct = SLang_create_array1 (c_cl->cl_data_type, 0, NULL, ct->dims, ct->num_dims, 1);
	if (ct == NULL)
	  return -1;
     }


   if ((na == 0) || (nb == 0)	       /* allow empty arrays */
       || (1 == (*binary_fun) (op, a_type, ap, na, b_type, bp, nb, ct->data)))
     {
	*(SLang_Array_Type **) cp = ct;
	return 1;
     }

   SLang_free_array (ct);
   return -1;
}

static int array_eqs_method (SLtype a_type, VOID_STAR ap, SLtype b_type, VOID_STAR bp)
{
   SLang_Array_Type *at, *bt, *ct;
   unsigned int i, num_dims, num_elements;
   SLang_Class_Type *a_cl, *b_cl, *c_cl;
   int is_eqs;
   int *ip, *ipmax;

   if ((a_type != SLANG_ARRAY_TYPE) || (b_type != SLANG_ARRAY_TYPE))
     return 0;
   
   at = *(SLang_Array_Type **) ap;
   bt = *(SLang_Array_Type **) bp;
   
   if (at == bt)
     return 1;

   if ((at->num_elements != (num_elements = bt->num_elements))
       || (at->num_dims != (num_dims = bt->num_dims)))
     return 0;
   
   for (i = 0; i < num_dims; i++)
     {
	if (at->dims[i] != bt->dims[i])
	  return 0;
     }

   a_type = at->data_type;
   b_type = bt->data_type;
   
   /* Check for an array of arrays.  If so, the arrays must reference the same set arrays */
   if ((a_type == SLANG_ARRAY_TYPE) || (b_type == SLANG_ARRAY_TYPE))
     {
	if (a_type != b_type)
	  return 0;
	
	return !memcmp ((char *)at->data, (char *)bt->data, num_elements*sizeof(SLang_Array_Type*));
     }

   a_cl = _pSLclass_get_class (a_type);
   b_cl = _pSLclass_get_class (b_type);

   if ((a_cl == b_cl)
       && ((a_cl->cl_class_type == SLANG_CLASS_TYPE_SCALAR)
	   || (a_cl->cl_class_type == SLANG_CLASS_TYPE_VECTOR)))
     {
	if ((-1 == coerse_array_to_linear (at))
	    || (-1 == coerse_array_to_linear (bt)))
	  return -1;
	
	return !memcmp ((char *)at->data, (char *)bt->data, num_elements*at->sizeof_type);
     }
   
   /* Do it the hard way */

   if (NULL == _pSLclass_get_binary_fun (SLANG_EQ, a_cl, b_cl, &c_cl, 0))
     return 0;

   if (num_elements == 0)
     return 1;

   if (-1 == array_binary_op (SLANG_EQ, SLANG_ARRAY_TYPE, ap, 1, SLANG_ARRAY_TYPE, bp, 1,
			      (VOID_STAR) &ct))
     return -1;
   
   /* ct is linear */
   num_elements = ct->num_elements;
   is_eqs = 1;
   if ((ct->data_type == SLANG_CHAR_TYPE) || (ct->data_type == SLANG_UCHAR_TYPE))
     {
	unsigned char *p, *pmax;

	p = (unsigned char *)ct->data;
	pmax = p + num_elements;
	
	while (p < pmax)
	  {
	     if (*p == 0)
	       {
		  is_eqs = 0;
		  break;
	       }
	     p++;
	  }
	SLang_free_array (ct);
	return is_eqs;
     }
   
   if (ct->data_type != SLANG_INT_TYPE)
     {
	SLang_Array_Type *tmp;
	if (1 != _pSLarray_typecast (ct->data_type, (VOID_STAR) &ct, 1,
				    SLANG_INT_TYPE, (VOID_STAR) &tmp, 1))
	  {
	     SLang_free_array (ct);
	     return -1;
	  }
	SLang_free_array (ct);
	ct = tmp;
     }
   
   ip = (int *)ct->data;
   ipmax = ip + num_elements;
   
   while (ip < ipmax)
     {
	if (*ip == 0)
	  {
	     is_eqs = 0;
	     break;
	  }
	ip++;
     }
   SLang_free_array (ct);
   return is_eqs;
}

static void is_null_intrinsic (SLang_Array_Type *at)
{
   SLang_Array_Type *bt;

   bt = SLang_create_array (SLANG_CHAR_TYPE, 0, NULL, at->dims, at->num_dims);
   if (bt == NULL)
     return;

   if (at->flags & SLARR_DATA_VALUE_IS_POINTER)
     {
	char *cdata, *cdata_max;
	char **data;

	if (-1 == coerse_array_to_linear (at))
	  {
	     SLang_free_array (bt);
	     return;
	  }
	
	cdata = (char *)bt->data;
	cdata_max = cdata + bt->num_elements;
	data = (char **)at->data;
	
	while (cdata < cdata_max)
	  {
	     if (*data == NULL)
	       *cdata = 1;
	     
	     data++;
	     cdata++;
	  }
     }
   
   SLang_push_array (bt, 1);
}
   
static SLang_Array_Type *pop_bool_array (void)
{
   SLang_Array_Type *at;
   SLang_Array_Type *tmp_at;
   int zero;

   if (-1 == SLang_pop_array (&at, 1))
     return NULL;

   if (at->data_type == SLANG_CHAR_TYPE)
     return at;

   tmp_at = at;
   zero = 0;
   if (1 != array_binary_op (SLANG_NE,
			     SLANG_ARRAY_TYPE, (VOID_STAR) &at, 1,
			     SLANG_CHAR_TYPE, (VOID_STAR) &zero, 1,
			     (VOID_STAR) &tmp_at))
     {
	SLang_free_array (at);
	return NULL;
     }

   SLang_free_array (at);
   at = tmp_at;
   if (at->data_type != SLANG_CHAR_TYPE)
     {
	SLang_free_array (at);
	SLang_set_error (SL_TYPE_MISMATCH);
	return NULL;
     }
   return at;
}

static int pop_bool_array_and_start (int nargs, SLang_Array_Type **atp, SLindex_Type *sp)
{
   SLang_Array_Type *at;
   SLindex_Type istart = *sp;
   SLindex_Type num_elements;

   if (nargs == 2)
     {
	if (-1 == SLang_pop_int (&istart))
	  return -1;
     }

   if (NULL == (at = pop_bool_array ()))
     return -1;

   num_elements = (SLindex_Type) at->num_elements;

   if (istart < 0)
     istart += num_elements;

   if (istart < 0)
     {
	if (num_elements == 0)
	  istart = 0;
	else
	  {
	     SLang_set_error (SL_Index_Error);
	     SLang_free_array (at);
	     return -1;
	  }
     }
   
   *atp = at;
   *sp = istart;
   return 0;
}

/* Usage: i = wherefirst (at [,startpos]); */
static void array_where_first (void)
{
   SLang_Array_Type *at;
   char *a_data;
   SLindex_Type i, num_elements;
   SLindex_Type istart = 0;
   
   istart = 0;
   if (-1 == pop_bool_array_and_start (SLang_Num_Function_Args, &at, &istart))
     return;

   a_data = (char *) at->data;
   num_elements = (SLindex_Type) at->num_elements;

   for (i = istart; i < num_elements; i++)
     {
	if (a_data[i] == 0)
	  continue;
	
	(void) SLang_push_int (i);
	SLang_free_array (at);
	return;
     }
   SLang_free_array (at);
   SLang_push_null ();
}

/* Usage: i = wherelast (at [,startpos]); */
static void array_where_last (void)
{
   SLang_Array_Type *at;
   char *a_data;
   SLindex_Type i;
   SLindex_Type istart = 0;
   
   istart = -1;
   if (-1 == pop_bool_array_and_start (SLang_Num_Function_Args, &at, &istart))
     return;

   a_data = (char *) at->data;

   i = istart + 1;
   if (i > (SLindex_Type)at->num_elements)
     i = (SLindex_Type) at->num_elements;
   while (i > 0)
     {
	i--;
	if (a_data[i] == 0)
	  continue;
	
	(void) SLang_push_int (i);
	SLang_free_array (at);
	return;
     }   
   SLang_free_array (at);
   SLang_push_null ();
}

static void array_where (void)
{
   SLang_Array_Type *at, *bt;
   char *a_data;
   SLindex_Type *b_data;
   SLuindex_Type i, num_elements;
   SLindex_Type b_num;

   if (NULL == (at = pop_bool_array ()))
     return;

   a_data = (char *) at->data;
   num_elements = at->num_elements;

   b_num = 0;
   for (i = 0; i < num_elements; i++)
     if (a_data[i] != 0) b_num++;

   if (NULL == (bt = SLang_create_array1 (SLANG_ARRAY_INDEX_TYPE, 0, NULL, &b_num, 1, 1)))
     goto return_error;

   b_data = (SLindex_Type *) bt->data;

   i = 0;
   while (b_num)
     {
	if (a_data[i] != 0)
	  {
	     *b_data++ = i;
	     b_num--;
	  }

	i++;
     }

   (void) SLang_push_array (bt, 0);
   /* drop */

   return_error:
   SLang_free_array (at);
   SLang_free_array (bt);
}

/* Up to the caller to ensure that ind_at is an index array */
static int do_array_reshape (SLang_Array_Type *at, SLang_Array_Type *ind_at)
{
   SLindex_Type *dims;
   unsigned int i, num_dims;
   SLuindex_Type num_elements;

   num_dims = ind_at->num_elements;
   dims = (SLindex_Type *) ind_at->data;

   num_elements = 1;
   for (i = 0; i < num_dims; i++)
     {
	SLindex_Type d = dims[i];
	if (d < 0)
	  {
	     SLang_verror (SL_INVALID_PARM, "reshape: dimension is less then 0");
	     return -1;
	  }

	num_elements = (SLuindex_Type) (d * num_elements);
     }

   if ((num_elements != at->num_elements)
       || (num_dims > SLARRAY_MAX_DIMS))
     {
	SLang_verror (SL_INVALID_PARM, "Unable to reshape array to specified size");
	return -1;
     }

   for (i = 0; i < num_dims; i++)
     at->dims [i] = dims[i];

   while (i < SLARRAY_MAX_DIMS)
     {
	at->dims [i] = 1;
	i++;
     }

   at->num_dims = num_dims;
   return 0;
}

static int pop_1d_index_array (SLang_Array_Type **ind_atp)
{
   SLang_Array_Type *ind_at;

   *ind_atp = NULL;
   if (-1 == SLang_pop_array_of_type (&ind_at, SLANG_ARRAY_INDEX_TYPE))
     return -1;
   if (ind_at->num_dims != 1)
     {
	SLang_verror (SL_TYPE_MISMATCH, "Expecting 1-d array of indices");
	return -1;
     }
   *ind_atp = ind_at;
   return 0;
}

static int pop_reshape_args (SLang_Array_Type **at, SLang_Array_Type **ind_at)
{
   if (-1 == pop_1d_index_array (ind_at))
     {
	*ind_at = *at = NULL;
	return -1;
     }
   if (-1 == SLang_pop_array (at, 1))
     {
	SLang_free_array (*ind_at);
	*ind_at = *at = NULL;
	return -1;
     }
   return 0;
}

static void array_reshape (void)
{
   SLang_Array_Type *at, *ind_at;

   if (-1 == pop_reshape_args (&at, &ind_at))
     return;
   (void) do_array_reshape (at, ind_at);
   SLang_free_array (at);
   SLang_free_array (ind_at);
}

static void _array_reshape (void)
{
   SLang_Array_Type *at;
   SLang_Array_Type *new_at;
   SLang_Array_Type *ind_at;
   
   if (-1 == pop_reshape_args (&at, &ind_at))
     return;

   /* FIXME: Priority=low: duplicate_array could me modified to look at num_refs */

   /* Now try to avoid the overhead of creating a new array if possible */
   if (at->num_refs == 1)
     {
	/* Great, we are the sole owner of this array. */
	if ((-1 == do_array_reshape (at, ind_at))
	    || (-1 == SLclass_push_ptr_obj (SLANG_ARRAY_TYPE, (VOID_STAR)at)))
	  SLang_free_array (at);
	SLang_free_array (ind_at);
	return;
     }

   new_at = SLang_duplicate_array (at);
   if (new_at != NULL)
     {
	if (0 == do_array_reshape (new_at, ind_at))
	  (void) SLang_push_array (new_at, 0);
	
	SLang_free_array (new_at);
     }
   SLang_free_array (at);
   SLang_free_array (ind_at);
}

typedef struct
{
   SLang_Array_Type *at;
   int is_array;
   size_t increment;
   char *addr;
}
Map_Arg_Type;
/* Usage: array_map (Return-Type, func, args,....); */
static void array_map (void)
{
   Map_Arg_Type *args;
   unsigned int num_args;
   unsigned int i, i_control;
   SLang_Name_Type *nt;
   unsigned int num_elements;
   SLang_Array_Type *at;
   char *addr;
   SLtype type;

   at = NULL;
   args = NULL;
   nt = NULL;

   if (SLang_Num_Function_Args < 3)
     {
	SLang_verror (SL_INVALID_PARM,
		      "Usage: array_map (Return-Type, &func, args...)");
	SLdo_pop_n (SLang_Num_Function_Args);
	return;
     }

   num_args = (unsigned int)SLang_Num_Function_Args - 2;
   args = (Map_Arg_Type *) SLmalloc (num_args * sizeof (Map_Arg_Type));
   if (args == NULL)
     {
	SLdo_pop_n (SLang_Num_Function_Args);
	return;
     }
   memset ((char *) args, 0, num_args * sizeof (Map_Arg_Type));
   i = num_args;
   i_control = 0;
   while (i > 0)
     {
	i--;
	if (SLANG_ARRAY_TYPE == SLang_peek_at_stack ())
	  {
	     args[i].is_array = 1;
	     i_control = i;
	  }

	if (-1 == SLang_pop_array (&args[i].at, 1))
	  {
	     SLdo_pop_n (i + 2);
	     goto return_error;
	  }
     }

   if (NULL == (nt = SLang_pop_function ()))
     {
	SLdo_pop_n (1);
	goto return_error;
     }

   num_elements = args[i_control].at->num_elements;

   if (-1 == SLang_pop_datatype (&type))
     goto return_error;

   if (type == SLANG_UNDEFINED_TYPE)   /* Void_Type */
     at = NULL;
   else
     {
	at = args[i_control].at;

	if (NULL == (at = SLang_create_array (type, 0, NULL, at->dims, at->num_dims)))
	  goto return_error;
     }
   

   for (i = 0; i < num_args; i++)
     {
	SLang_Array_Type *ati = args[i].at;
	/* FIXME: Priority = low: The actual dimensions should be compared. */
	if (ati->num_elements == num_elements)
	  args[i].increment = ati->sizeof_type;
	/* memset already guarantees increment to be zero */

	if ((num_elements != 0) 
	    && (ati->num_elements == 0))
	  {
	     SLang_verror (SL_TypeMismatch_Error, "array_map: function argument %d of %d is an empty array", 
			   i+1, num_args);
	     goto return_error;
	  }

	args[i].addr = (char *) ati->data;
     }

   if (at == NULL)
     addr = NULL;
   else
     addr = (char *)at->data;

   for (i = 0; i < num_elements; i++)
     {
	unsigned int j;

	if (-1 == SLang_start_arg_list ())
	  goto return_error;

	for (j = 0; j < num_args; j++)
	  {
	     if (-1 == push_element_at_addr (args[j].at, 
					     (VOID_STAR) args[j].addr,
					     1))
	       {
		  SLdo_pop_n (j);
		  goto return_error;
	       }

	     args[j].addr += args[j].increment;
	  }

	if (-1 == SLang_end_arg_list ())
	  {
	     SLdo_pop_n (num_args);
	     goto return_error;
	  }

	if (-1 == SLexecute_function (nt))
	  goto return_error;

	if (at == NULL)
	  continue;

	if (-1 == at->cl->cl_apop (type, (VOID_STAR) addr))
	  goto return_error;

	addr += at->sizeof_type;
     }

   if (at != NULL)
     (void) SLang_push_array (at, 0);

   /* drop */

   return_error:
   SLang_free_array (at);
   SLang_free_function (nt);
   if (args != NULL)
     {
	for (i = 0; i < num_args; i++)
	  SLang_free_array (args[i].at);

	SLfree ((char *) args);
     }
}

static int push_array_shape (SLang_Array_Type *at)
{
   SLang_Array_Type *bt;
   SLindex_Type num_dims;
   SLindex_Type *bdata, *a_dims;
   int i;

   num_dims = (SLindex_Type)at->num_dims;
   if (NULL == (bt = SLang_create_array (SLANG_ARRAY_INDEX_TYPE, 0, NULL, &num_dims, 1)))
     return -1;

   a_dims = at->dims;
   bdata = (SLindex_Type *) bt->data;
   for (i = 0; i < num_dims; i++) bdata [i] = a_dims [i];

   return SLang_push_array (bt, 1);
}

static void array_info (void)
{
   SLang_Array_Type *at;

   if (-1 == pop_array (&at, 1))
     return;

   if (0 == push_array_shape (at))
     {
	(void) SLang_push_integer ((int) at->num_dims);
	(void) SLang_push_datatype (at->data_type);
     }
   SLang_free_array (at);
}

static void array_shape (void)
{
   SLang_Array_Type *at;

   if (-1 == pop_array (&at, 1))
     return;

   (void) push_array_shape (at);
   SLang_free_array (at);
}

#if 0
static int pop_int_indices (SLindex_Type *dims, unsigned int ndims)
{
   int i;

   if (ndims > SLARRAY_MAX_DIMS)
     {
	SLang_verror (SL_INVALID_PARM, "Too many dimensions specified");
	return -1;
     }
   for (i = (int)ndims-1; i >= 0; i--)
     {
	if (-1 == SLang_pop_integer (dims+i))
	  return -1;
     }
   return 0;
}
/* Usage: aput(v, x, i,..,k) */
static void aput_intrin (void)
{
   char *data_to_put;
   SLuindex_Type data_increment;
   SLindex_Type indices[SLARRAY_MAX_DIMS];
   int is_ptr;
   unsigned int ndims = SLang_Num_Function_Args-2;
   SLang_Array_Type *at, *bt_unused;
   
   if (-1 == pop_int_indices (indices, ndims))
     return;

   if (-1 == SLang_pop_array (&at, 1))
     return;

   if (at->num_dims != ndims)
     {
	SLang_set_error (SL_Index_Error);
	SLang_free_array (at);
	return;
     }
   
   is_ptr = (at->flags & SLARR_DATA_VALUE_IS_POINTER);

   if (-1 == aput_get_data_to_put (at->cl, 1, 0, &bt_unused, &data_to_put, &data_increment))
     {
	SLang_free_array (at);
	return;
     }
   
   (void) _pSLarray_aput_transfer_elem (at, indices, (VOID_STAR)data_to_put, 
				       at->sizeof_type, is_ptr);
   if (is_ptr)
     (*at->cl->cl_destroy) (at->cl->cl_data_type, (VOID_STAR) data_to_put);
   
   SLang_free_array (at);
}

/* Usage: x_i..k = aget(x, i,..,k) */
static void aget_intrin (void)
{
   SLindex_Type dims[SLARRAY_MAX_DIMS];
   unsigned int ndims = (unsigned int) (SLang_Num_Function_Args-1);
   SLang_Array_Type *at;
   VOID_STAR data;

   if (-1 == pop_int_indices (dims, ndims))
     return;

   if (-1 == pop_array (&at, 1))
     return;

   if (at->num_dims != ndims)
     {
	SLang_set_error (SL_Index_Error);
	SLang_free_array (at);
	return;
     }

   if ((ndims == 1)
       && (at->index_fun == linear_get_data_addr))
     {
	SLindex_Type i = dims[0];
	if (i < 0) 
	  {
	     i += at->dims[0];
	     if (i < 0)
	       i = at->dims[0];
	  }
	if (i >= at->dims[0])
	  {
	     SLang_set_error (SL_Index_Error);
	     SLang_free_array (at);
	     return;
	  }
	if (at->data_type == SLANG_INT_TYPE)
	  {
	     (void) SLclass_push_int_obj (SLANG_INT_TYPE, *((int *)at->data + i));
	     goto free_and_return;
	  }
#if SLANG_HAS_FLOAT
	if (at->data_type == SLANG_DOUBLE_TYPE)
	  {
	     (void) SLclass_push_double_obj (SLANG_DOUBLE_TYPE, *((double *)at->data + i));
	     goto free_and_return;
	  }
#endif
	if (at->data_type == SLANG_CHAR_TYPE)
	  {
	     (void) SLclass_push_int_obj (SLANG_UCHAR_TYPE, *((unsigned char *)at->data + i));
	     goto free_and_return;
	  }
	data = (VOID_STAR) ((char *)at->data + (SLuindex_Type)i * at->sizeof_type);
     }
   else data = get_data_addr (at, dims);
   
   if (data != NULL)
     (void) push_element_at_addr (at, (VOID_STAR) data, ndims);

   free_and_return:
   SLang_free_array (at);
}
#endif

static SLang_Intrin_Fun_Type Array_Table [] =
{
   MAKE_INTRINSIC_0("array_map", array_map, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("array_sort", sort_array, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_1("array_to_bstring", array_to_bstring, SLANG_VOID_TYPE, SLANG_ARRAY_TYPE),
   MAKE_INTRINSIC_1("bstring_to_array", bstring_to_array, SLANG_VOID_TYPE, SLANG_BSTRING_TYPE),
   MAKE_INTRINSIC("init_char_array", init_char_array, SLANG_VOID_TYPE, 0),
   MAKE_INTRINSIC_1("_isnull", is_null_intrinsic, SLANG_VOID_TYPE, SLANG_ARRAY_TYPE),
   MAKE_INTRINSIC_0("array_info", array_info, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("array_shape", array_shape, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("where", array_where, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("wherefirst", array_where_first, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("wherelast", array_where_last, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("reshape", array_reshape, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("_reshape", _array_reshape, SLANG_VOID_TYPE),
#if 0
   MAKE_INTRINSIC_0("__aget", aget_intrin, SLANG_VOID_TYPE),
   MAKE_INTRINSIC_0("__aput", aput_intrin, SLANG_VOID_TYPE),
#endif
   SLANG_END_INTRIN_FUN_TABLE
};

static char *array_string (SLtype type, VOID_STAR v)
{
   SLang_Array_Type *at;
   char buf[512];
   unsigned int i, num_dims;
   SLindex_Type *dims;

   at = *(SLang_Array_Type **) v;
   type = at->data_type;
   num_dims = at->num_dims;
   dims = at->dims;

   sprintf (buf, "%s[%ld", SLclass_get_datatype_name (type), (long)at->dims[0]);

   for (i = 1; i < num_dims; i++)
     sprintf (buf + strlen(buf), ",%ld", (long)dims[i]);
   strcat (buf, "]");

   return SLmake_string (buf);
}

static void array_destroy (SLtype type, VOID_STAR v)
{
   (void) type;
   SLang_free_array (*(SLang_Array_Type **) v);
}

static int array_push (SLtype type, VOID_STAR v)
{
   SLang_Array_Type *at;

   (void) type;
   at = *(SLang_Array_Type **) v;
   return SLang_push_array (at, 0);
}

/* Intrinsic arrays are not stored in a variable. So, the address that
 * would contain the variable holds the array address.
 */
static int array_push_intrinsic (SLtype type, VOID_STAR v)
{
   (void) type;
   return SLang_push_array ((SLang_Array_Type *) v, 0);
}

int _pSLarray_add_bin_op (SLtype type)
{
   SL_OOBinary_Type *ab;
   SLang_Class_Type *cl;

   if (type == SLANG_VOID_TYPE)
     {
	cl = _pSLclass_get_class (SLANG_ARRAY_TYPE);
	if ((cl->cl_this_binary_void != NULL)
	    || (cl->cl_void_binary_this != NULL))
	  return 0;
     }
   else
     {
	cl = _pSLclass_get_class (type);
	ab = cl->cl_binary_ops;

	while (ab != NULL)
	  {
	     if (ab->data_type == SLANG_ARRAY_TYPE)
	       return 0;
	     ab = ab->next;
	  }
     }

   if ((-1 == SLclass_add_binary_op (SLANG_ARRAY_TYPE, type, array_binary_op, array_binary_op_result))
       || (-1 == SLclass_add_binary_op (type, SLANG_ARRAY_TYPE, array_binary_op, array_binary_op_result)))
     return -1;

   return 0;
}


static SLang_Array_Type *
do_array_math_op (int op, int unary_type,
		  SLang_Array_Type *at, unsigned int na)
{
   SLtype a_type, b_type;
   int (*f) (int, SLtype, VOID_STAR, unsigned int, VOID_STAR);
   SLang_Array_Type *bt;
   SLang_Class_Type *b_cl;
   int no_init;

   if (na != 1)
     {
	SLang_verror (SL_NOT_IMPLEMENTED, "Operation restricted to 1 array");
	return NULL;
     }

   a_type = at->data_type;
   if (NULL == (f = _pSLclass_get_unary_fun (op, at->cl, &b_cl, unary_type)))
     return NULL;
   b_type = b_cl->cl_data_type;

   if (-1 == coerse_array_to_linear (at))
     return NULL;

   no_init = ((b_cl->cl_class_type == SLANG_CLASS_TYPE_SCALAR)
	      || (b_cl->cl_class_type == SLANG_CLASS_TYPE_VECTOR));

#if SLANG_USE_TMP_OPTIMIZATION
   /* If we are dealing with scalar (or vector) objects, and if the object
    * appears to be owned by the stack, then use it instead of creating a 
    * new version.  This can happen with code such as:
    * @  x = [1,2,3,4];
    * @  x = UNARY_OP(__tmp(x));
    */
   if (no_init
       && (at->num_refs == 1)
       && (at->data_type == b_cl->cl_data_type))
     {
	bt = at;
	bt->num_refs = 2;
     }
   else
#endif				       /* SLANG_USE_TMP_OPTIMIZATION */
     if (NULL == (bt = SLang_create_array1 (b_type, 0, NULL, at->dims, at->num_dims, 1)))
       return NULL;

   if (1 != (*f)(op, a_type, at->data, at->num_elements, bt->data))
     {
	SLang_free_array (bt);
	return NULL;
     }
   return bt;
}

static int
array_unary_op_result (int op, SLtype a, SLtype *b)
{
   (void) op;
   (void) a;
   *b = SLANG_ARRAY_TYPE;
   return 1;
}

static int
array_unary_op (int op,
		SLtype a, VOID_STAR ap, unsigned int na,
		VOID_STAR bp)
{
   SLang_Array_Type *at;

   (void) a;
   at = *(SLang_Array_Type **) ap;
   if (NULL == (at = do_array_math_op (op, SLANG_BC_UNARY, at, na)))
     {
	if (SLang_get_error ()) return -1;
	return 0;
     }
   *(SLang_Array_Type **) bp = at;
   return 1;
}

static int
array_math_op (int op,
	       SLtype a, VOID_STAR ap, unsigned int na,
	       VOID_STAR bp)
{
   SLang_Array_Type *at;

   (void) a;
   at = *(SLang_Array_Type **) ap;
   if (NULL == (at = do_array_math_op (op, SLANG_BC_MATH_UNARY, at, na)))
     {
	if (SLang_get_error ()) return -1;
	return 0;
     }
   *(SLang_Array_Type **) bp = at;
   return 1;
}

static int
array_app_op (int op,
	      SLtype a, VOID_STAR ap, unsigned int na,
	      VOID_STAR bp)
{
   SLang_Array_Type *at;

   (void) a;
   at = *(SLang_Array_Type **) ap;
   if (NULL == (at = do_array_math_op (op, SLANG_BC_APP_UNARY, at, na)))
     {
	if (SLang_get_error ()) return -1;
	return 0;
     }
   *(SLang_Array_Type **) bp = at;
   return 1;
}

/* Typecast array from a_type to b_type */
int
_pSLarray_typecast (SLtype a_type, VOID_STAR ap, unsigned int na,
		   SLtype b_type, VOID_STAR bp,
		   int is_implicit)
{
   SLang_Array_Type *at, *bt;
   SLang_Class_Type *b_cl;
   int no_init;
   int (*t) (SLtype, VOID_STAR, unsigned int, SLtype, VOID_STAR);

   if (na != 1)
     {
	SLang_verror (SL_NOT_IMPLEMENTED, "typecast of multiple arrays not implemented");
	return -1;
     }

   at = *(SLang_Array_Type **) ap;
   a_type = at->data_type;

   if (a_type == b_type)
     {
	at->num_refs += 1;
	*(SLang_Array_Type **) bp = at;
	return 1;
     }

   /* check for alias */
   if (at->cl == (b_cl = _pSLclass_get_class (b_type)))
     {
	at->num_refs += 1;
	
	/* Force to the desired type.  Hopefully there will be no consequences 
	 * from this.
	 */
	at->data_type = b_cl->cl_data_type;
	*(SLang_Array_Type **) bp = at;
	return 1;
     }
   
   if (at->flags & SLARR_DATA_VALUE_IS_RANGE)
     {
	if (-1 == try_typecast_range_array (at, b_type, &bt))
	  return -1;
	if (bt != NULL)
	  {
	     *(SLang_Array_Type **) bp = bt;
	     return 1;
	  }
	/* Couldn't do it, so drop */
     }

   /* Typecast NULL array to the desired type with elements set to NULL */
   if ((a_type == SLANG_NULL_TYPE) 
       && ((b_cl->cl_class_type == SLANG_CLASS_TYPE_MMT)
	   || (b_cl->cl_class_type == SLANG_CLASS_TYPE_PTR)))
     {
	if (NULL == (bt = SLang_create_array1 (b_type, 0, NULL, at->dims, at->num_dims, 0)))
	  return -1;

	*(SLang_Array_Type **) bp = bt;
	return 1;
     }

   
   if (NULL == (t = _pSLclass_get_typecast (a_type, b_type, is_implicit)))
     return -1;

   if (-1 == coerse_array_to_linear (at))
     return -1;

   no_init = ((b_cl->cl_class_type == SLANG_CLASS_TYPE_SCALAR)
	      || (b_cl->cl_class_type == SLANG_CLASS_TYPE_VECTOR));

   if (NULL == (bt = SLang_create_array1 (b_type, 0, NULL, at->dims, at->num_dims, no_init)))
     return -1;

   if (1 == (*t) (a_type, at->data, at->num_elements, b_type, bt->data))
     {
	*(SLang_Array_Type **) bp = bt;
	return 1;
     }

   SLang_free_array (bt);
   return 0;
}

SLang_Array_Type *SLang_duplicate_array (SLang_Array_Type *at)
{
   SLang_Array_Type *bt;
   char *data, *a_data;
   SLuindex_Type i, num_elements;
   size_t sizeof_type, size;
   int (*cl_acopy) (SLtype, VOID_STAR, VOID_STAR);
   SLtype type;

   if (-1 == coerse_array_to_linear (at))
     return NULL;

   type = at->data_type;
   num_elements = at->num_elements;
   sizeof_type = at->sizeof_type;
   size = num_elements * sizeof_type;

   if (NULL == (data = SLmalloc (size)))
     return NULL;

   if (NULL == (bt = SLang_create_array (type, 0, (VOID_STAR)data, at->dims, at->num_dims)))
     {
	SLfree (data);
	return NULL;
     }

   a_data = (char *) at->data;
   if (0 == (at->flags & SLARR_DATA_VALUE_IS_POINTER))
     {
	SLMEMCPY (data, a_data, size);
	return bt;
     }

   SLMEMSET (data, 0, size);

   cl_acopy = at->cl->cl_acopy;
   for (i = 0; i < num_elements; i++)
     {
	if (NULL != *(VOID_STAR *) a_data)
	  {
	     if (-1 == (*cl_acopy) (type, (VOID_STAR) a_data, (VOID_STAR) data))
	       {
		  SLang_free_array (bt);
		  return NULL;
	       }
	  }

	data += sizeof_type;
	a_data += sizeof_type;
     }

   return bt;
}

static int array_dereference (SLtype type, VOID_STAR addr)
{
   SLang_Array_Type *at;

   (void) type;
   at = SLang_duplicate_array (*(SLang_Array_Type **) addr);
   if (at == NULL) return -1;
   return SLang_push_array (at, 1);
}

/* This function gets called via, e.g., @Array_Type (Double_Type, [10,20]);
 */
static int
array_datatype_deref (SLtype type)
{
   SLang_Array_Type *ind_at;
   SLang_Array_Type *at;

#if 0
   /* The parser generated code for this as if a function call were to be
    * made.  However, the interpreter simply called the deref object routine
    * instead of the function call.  So, I must simulate the function call.
    * This needs to be formalized to hide this detail from applications
    * who wish to do the same.  So...
    * FIXME: Priority=medium
    */
   if (0 == _pSL_increment_frame_pointer ())
     (void) _pSL_decrement_frame_pointer ();
#endif

   if (-1 == pop_1d_index_array (&ind_at))
     goto return_error;

   if (-1 == SLang_pop_datatype (&type))
     goto return_error;

   if (NULL == (at = SLang_create_array (type, 0, NULL,
					 (SLindex_Type *) ind_at->data,
					 ind_at->num_elements)))
     goto return_error;

   SLang_free_array (ind_at);
   return SLang_push_array (at, 1);

   return_error:
   SLang_free_array (ind_at);
   return -1;
}

static int array_length (SLtype type, VOID_STAR v, unsigned int *len)
{
   SLang_Array_Type *at;

   (void) type;
   at = *(SLang_Array_Type **) v;
   *len = at->num_elements;
   return 0;
}

int
_pSLarray_init_slarray (void)
{
   SLang_Class_Type *cl;

   if (-1 == SLadd_intrin_fun_table (Array_Table, NULL))
     return -1;

   if (NULL == (cl = SLclass_allocate_class ("Array_Type")))
     return -1;

   (void) SLclass_set_string_function (cl, array_string);
   (void) SLclass_set_destroy_function (cl, array_destroy);
   (void) SLclass_set_push_function (cl, array_push);
   cl->cl_push_intrinsic = array_push_intrinsic;
   cl->cl_dereference = array_dereference;
   cl->cl_datatype_deref = array_datatype_deref;
   cl->cl_length = array_length;
   cl->is_container = 1;

   (void) SLclass_set_eqs_function (cl, array_eqs_method);

   if (-1 == SLclass_register_class (cl, SLANG_ARRAY_TYPE, sizeof (VOID_STAR),
				     SLANG_CLASS_TYPE_PTR))
     return -1;

   if ((-1 == SLclass_add_binary_op (SLANG_ARRAY_TYPE, SLANG_ARRAY_TYPE, array_binary_op, array_binary_op_result))
       || (-1 == SLclass_add_unary_op (SLANG_ARRAY_TYPE, array_unary_op, array_unary_op_result))
       || (-1 == SLclass_add_app_unary_op (SLANG_ARRAY_TYPE, array_app_op, array_unary_op_result))
       || (-1 == SLclass_add_math_op (SLANG_ARRAY_TYPE, array_math_op, array_unary_op_result)))
     return -1;

   return 0;
}

int SLang_pop_array (SLang_Array_Type **at_ptr, int convert_scalar)
{
   if (-1 == pop_array (at_ptr, convert_scalar))
     return -1;

   if (-1 == coerse_array_to_linear (*at_ptr))
     {
	SLang_free_array (*at_ptr);
	return -1;
     }
   return 0;
}

int SLang_pop_array_of_type (SLang_Array_Type **at, SLtype type)
{
   if (-1 == SLclass_typecast (type, 1, 1))
     return -1;

   return SLang_pop_array (at, 1);
}

void (*_pSLang_Matrix_Multiply)(void);

int _pSLarray_matrix_multiply (void)
{
   if (_pSLang_Matrix_Multiply != NULL)
     {
	(*_pSLang_Matrix_Multiply)();
	return 0;
     }
   SLang_verror (SL_NOT_IMPLEMENTED, "Matrix multiplication not available");
   return -1;
}

struct _pSLang_Foreach_Context_Type
{
   SLang_Array_Type *at;
   SLindex_Type next_element_index;
};

SLang_Foreach_Context_Type *
_pSLarray_cl_foreach_open (SLtype type, unsigned int num)
{
   SLang_Foreach_Context_Type *c;

   if (num != 0)
     {
	SLdo_pop_n (num + 1);
	SLang_verror (SL_NOT_IMPLEMENTED,
		      "%s does not support 'foreach using' form",
		      SLclass_get_datatype_name (type));
	return NULL;
     }

   if (NULL == (c = (SLang_Foreach_Context_Type *) SLmalloc (sizeof (SLang_Foreach_Context_Type))))
     return NULL;

   memset ((char *) c, 0, sizeof (SLang_Foreach_Context_Type));

   if (-1 == pop_array (&c->at, 1))
     {
	SLfree ((char *) c);
	return NULL;
     }

   return c;
}

void _pSLarray_cl_foreach_close (SLtype type, SLang_Foreach_Context_Type *c)
{
   (void) type;
   if (c == NULL) return;
   SLang_free_array (c->at);
   SLfree ((char *) c);
}

int _pSLarray_cl_foreach (SLtype type, SLang_Foreach_Context_Type *c)
{
   SLang_Array_Type *at;
   VOID_STAR data;

   (void) type;

   if (c == NULL)
     return -1;

   at = c->at;
   if ((SLindex_Type)at->num_elements == c->next_element_index)
     return 0;

   /* FIXME: Priority = low.  The following assumes linear arrays
    * or Integer range arrays.  Fixing it right requires a method to get the
    * nth element of a multidimensional array.
    */

   if (at->flags & SLARR_DATA_VALUE_IS_RANGE)
     {
	SLindex_Type d = (SLindex_Type) c->next_element_index;
	data = range_get_data_addr (at, &d);
     }
   else
     data = (VOID_STAR) ((char *)at->data + (c->next_element_index * at->sizeof_type));

   c->next_element_index += 1;

   if ((at->flags & SLARR_DATA_VALUE_IS_POINTER)
       && (*(VOID_STAR *) data == NULL))
     {
	if (-1 == SLang_push_null ())
	  return -1;
     }
   else if (-1 == (*at->cl->cl_apush)(at->data_type, data))
     return -1;

   /* keep going */
   return 1;
}
