/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * spec.hpp: definitions describing DWARF standards and vendor extensions.
 *
 * Copyright (c) 2008--9, Stephen Kell.
 */

#ifndef DWARFPP_SPEC_HPP_
#define DWARFPP_SPEC_HPP_

#include <map>
#include <utility>
#include <cstdlib>
#include <iostream>
#include <cassert>
#include <algorithm>

namespace dwarf
{
	namespace spec
	{
		extern "C"
		{
			#include <dwarf.h>
		}
	}
}

#define DECLARE_STD \
		/* name--value mappings and basic functions/predicates */ \
		extern std::map<const char *, int> tag_forward_map; \
		extern std::map<int, const char *> tag_inverse_map; \
 \
		extern std::map<const char *, int> form_forward_map; \
		extern std::map<int, const char *> form_inverse_map; \
 \
		extern std::map<const char *, int> attr_forward_map; \
		extern std::map<int, const char *> attr_inverse_map; \
 \
		extern std::map<const char *, int> encoding_forward_map; \
		extern std::map<int, const char *> encoding_inverse_map; \
 \
		extern std::map<const char *, int> op_forward_map; \
		extern std::map<int, const char *> op_inverse_map; \
 \
		extern std::map<int, const int *> op_operand_forms_tbl; \
 \
//		/* basic predicates defining useful groupings of DWARF entries */ 
//		bool tag_is_type(int tag); 
//		bool tag_has_named_children(int tag); 
//		bool attr_describes_location(int attr); 
// 
//		/* basic functions over DWARF operations */ 
//		int op_operand_count(int op); 


namespace dwarf
{
	namespace spec
	{
		extern "C"
		{
			#include <dwarf.h>
		}
		DECLARE_STD
		
		/* DWARF defines a set of "classes" which determine how attributes
		 * may be interpreted, in a fashion not fully determined by their
		 * FORM (storage). */
		class interp
		{
			interp() {} // non-instantiable -- interp just defines a namespace
		public:
			enum cls
			{
				EOL = 0,
				reference,
				block,
				loclistptr,
				string,
				constant,
				lineptr,
				address,
				flag,
				macptr,
				rangelistptr,
				block_as_dwarf_expr = 0x20
			};
		};
		
		/* Each vendor's extensions get their own namespace.
		 * But note! Many vendor extensions are defined in dwarf.h,
		 * so will appear in dwarf::spec. However, I have specifically
		 * excluded these from the dwarf::spec lookup tables 
		 * (see spec.cpp) and FIXME will put them into the tables in the namespaces
		 * to follow.
		 *
		 * FIXME: use some Makefile grep/sed magic to pull vendor extensions
		 * out of dwarf.h, so that symbolic constants also appear in
		 * the right namespaces. If they appear in dwarf::spec:: too,
		 * no harm done. */
		namespace gnu_ext
		{
			DECLARE_STD
		}
		namespace hp_ext
		{
			DECLARE_STD
		}
		namespace sgi_ext
		{
			DECLARE_STD
		}
		// you get the idea...
	}
}

// generic list-making macros
#define PAIR_ENTRY_FORWARDS(sym) (std::make_pair(#sym, (sym))),
#define PAIR_ENTRY_BACKWARDS(sym) (std::make_pair((sym), #sym)),
#define PAIR_ENTRY_FORWARDS_LAST(sym) (std::make_pair(#sym, (sym)))
#define PAIR_ENTRY_BACKWARDS_LAST(sym) (std::make_pair((sym), #sym))

#define PAIR_ENTRY_QUAL_FORWARDS(qual, sym) (std::make_pair(#sym, (qual sym))),
#define PAIR_ENTRY_QUAL_BACKWARDS(qual, sym) (std::make_pair((qual sym), #sym)),
#define PAIR_ENTRY_QUAL_FORWARDS_LAST(qual, sym) (std::make_pair(#sym, (qual sym)))
#define PAIR_ENTRY_QUAL_BACKWARDS_LAST(qual, sym) (std::make_pair((qual sym), #sym))

#define PAIR_ENTRY_FORWARDS_VARARGS(sym, ...) (std::make_pair(#sym, (sym))),
#define PAIR_ENTRY_BACKWARDS_VARARGS(sym, ...) (std::make_pair((sym), #sym)),
#define PAIR_ENTRY_FORWARDS_VARARGS_LAST(sym, ...) (std::make_pair(#sym, (sym)))
#define PAIR_ENTRY_BACKWARDS_VARARGS_LAST(sym, ...) (std::make_pair((sym), #sym))

#define MAKE_LOOKUP(pair_type, name, make_pair, last_pair, list) \
pair_type name[] = { \
						list(make_pair, last_pair) \
					}
                    
#define TABLE_ARG_DECLS \
							forward_name_mapping_t (&tag_forward_tbl)[tag_tbl_size], \
							inverse_name_mapping_t (&tag_inverse_tbl)[tag_tbl_size], \
							forward_name_mapping_t (&attr_forward_tbl)[attr_tbl_size], \
							inverse_name_mapping_t (&attr_inverse_tbl)[attr_tbl_size], \
							attr_class_mapping_t   (&attr_class_tbl_array)[attr_tbl_size], \
							forward_name_mapping_t (&form_forward_tbl)[form_tbl_size], \
							inverse_name_mapping_t (&form_inverse_tbl)[form_tbl_size],	 \
							form_class_mapping_t   (&form_class_tbl_array)[form_tbl_size], \
							forward_name_mapping_t (&encoding_forward_tbl)[encoding_tbl_size], \
							inverse_name_mapping_t (&encoding_inverse_tbl)[encoding_tbl_size], \
							forward_name_mapping_t (&op_forward_tbl)[op_tbl_size], \
							inverse_name_mapping_t (&op_inverse_tbl)[op_tbl_size], \
							op_operand_forms_mapping_t (&op_operand_forms_tbl_array)[op_tbl_size], \
                            forward_name_mapping_t (&interp_forward_tbl)[interp_tbl_size], \
                            inverse_name_mapping_t (&interp_inverse_tbl)[interp_tbl_size]                             

#define TABLE_INITS \
						    tag_tbl_size(tag_tbl_size), \
						    tag_forward_tbl(tag_forward_tbl), \
						    tag_forward_map(&tag_forward_tbl[0], &tag_forward_tbl[sizeof tag_forward_tbl / sizeof (forward_name_mapping_t)]), \
						    tag_inverse_tbl(tag_inverse_tbl), \
						    tag_inverse_map(&tag_inverse_tbl[0], &tag_inverse_tbl[sizeof tag_inverse_tbl / sizeof (inverse_name_mapping_t)]), \
						    attr_tbl_size(attr_tbl_size), \
						    attr_forward_tbl(attr_forward_tbl), \
						    attr_forward_map(&attr_forward_tbl[0], &attr_forward_tbl[sizeof attr_forward_tbl / sizeof (forward_name_mapping_t)]), \
						    attr_inverse_tbl(attr_inverse_tbl), \
						    attr_inverse_map(&attr_inverse_tbl[0], &attr_inverse_tbl[sizeof attr_inverse_tbl / sizeof (inverse_name_mapping_t)]), \
						    attr_class_tbl_array(attr_class_tbl_array), \
						    attr_class_tbl(&attr_class_tbl_array[0], &attr_class_tbl_array[sizeof attr_class_tbl_array / sizeof (attr_class_mapping_t)]), \
						    form_tbl_size(form_tbl_size), \
						    form_forward_tbl(form_forward_tbl), \
						    form_forward_map(&form_forward_tbl[0], &form_forward_tbl[sizeof form_forward_tbl / sizeof (forward_name_mapping_t)]), \
						    form_inverse_tbl(form_inverse_tbl), \
						    form_inverse_map(&form_inverse_tbl[0], &form_inverse_tbl[sizeof form_inverse_tbl / sizeof (inverse_name_mapping_t)]), \
						    form_class_tbl_array(form_class_tbl_array), \
						    form_class_tbl(&form_class_tbl_array[0], &form_class_tbl_array[sizeof form_class_tbl_array / sizeof (form_class_mapping_t)]), \
						    encoding_tbl_size(encoding_tbl_size), \
						    encoding_forward_tbl(encoding_forward_tbl), \
						    encoding_forward_map(&encoding_forward_tbl[0], &encoding_forward_tbl[sizeof encoding_forward_tbl / sizeof (forward_name_mapping_t)]), \
						    encoding_inverse_tbl(encoding_inverse_tbl), \
						    encoding_inverse_map(&encoding_inverse_tbl[0], &encoding_inverse_tbl[sizeof encoding_inverse_tbl / sizeof (inverse_name_mapping_t)]), \
						    op_tbl_size(op_tbl_size), \
						    op_forward_tbl(op_forward_tbl), \
						    op_forward_map(&op_forward_tbl[0], &op_forward_tbl[sizeof op_forward_tbl / sizeof (forward_name_mapping_t)]), \
						    op_inverse_tbl(op_inverse_tbl), \
						    op_inverse_map(&op_inverse_tbl[0], &op_inverse_tbl[sizeof op_inverse_tbl / sizeof (inverse_name_mapping_t)]), \
						    op_operand_forms_tbl_array(op_operand_forms_tbl_array), \
						    op_operand_forms_tbl(&op_operand_forms_tbl_array[0], &op_operand_forms_tbl_array[sizeof op_operand_forms_tbl_array / sizeof (op_operand_forms_mapping_t)]), \
                            interp_tbl_size(interp_tbl_size), \
                            interp_forward_tbl(interp_forward_tbl), \
                            interp_forward_map(&interp_forward_tbl[0], &interp_forward_tbl[sizeof interp_forward_tbl / sizeof (forward_name_mapping_t)]), \
                            interp_inverse_tbl(interp_inverse_tbl), \
                            interp_inverse_map(&interp_inverse_tbl[0], &interp_inverse_tbl[sizeof interp_inverse_tbl / sizeof (inverse_name_mapping_t)])

namespace dwarf 
{
	namespace spec 
	{
		namespace table
		{
			typedef std::pair<const char *, int> forward_name_mapping_t;
			typedef std::pair<int, const char *> inverse_name_mapping_t;
			typedef std::pair<int, const int *> attr_class_mapping_t;	
			typedef std::pair<int, const int *> form_class_mapping_t;	
			typedef std::pair<int, const int *> op_operand_forms_mapping_t;	
		}
		using namespace table;

		class abstract_def
		{
		public:
			virtual const char *tag_lookup(int tag) const = 0;
			virtual bool tag_is_type(int tag) const = 0;
            virtual bool tag_is_type_chain(int tag) const = 0;
			virtual bool tag_has_named_children(int tag) const = 0;
			virtual const char *attr_lookup(int attr) const = 0;
            virtual const int *attr_get_classes(int attr) const = 0;
			virtual bool attr_describes_location(int attr) const = 0;
			virtual const char *form_lookup(int form) const = 0;
			virtual const int *form_get_classes(int form) const = 0;
			virtual const char *encoding_lookup(int encoding) const = 0;
			virtual const char *op_lookup(int op) const = 0;
            virtual bool op_reads_register(int op) const = 0;
			virtual size_t op_operand_count(int op) const = 0;
			virtual const int *op_operand_form_list(int op) const = 0;
            virtual int get_explicit_interp(int attr, int form) const = 0;
            virtual int get_interp(int attr, int form) const = 0;
            virtual const char *interp_lookup(int interp) const = 0;
			friend std::ostream& operator<<(std::ostream& o, const abstract_def& a);
            virtual std::ostream& print(std::ostream& o) const = 0;
		};
		std::ostream& operator<<(std::ostream& o, const abstract_def& a);
		class empty_def : public abstract_def
		{
			static const int empty_operand_form_list[];
            static const int empty_class_list[];
		public:
			empty_def() {}
            static const empty_def inst;
			virtual const char *tag_lookup(int tag) const { return "(unknown tag)"; }			
			virtual bool tag_is_type(int tag) const { return false; }
            virtual bool tag_is_type_chain(int tag) const { return false; }
			virtual bool tag_has_named_children(int tag) const { return false; }
			virtual const char *attr_lookup(int attr) const { return "(unknown attribute)"; }
	        virtual const int *attr_get_classes(int attr) const { return empty_class_list; }
			virtual bool attr_describes_location(int attr) const { return false; }
			virtual const char *form_lookup(int form) const { return "(unknown form)"; }
	        virtual const int *form_get_classes(int form) const { return empty_class_list; }
			virtual const char *encoding_lookup(int encoding) const { return "(unknown encoding)"; }
			virtual const char *op_lookup(int op) const { return "(unknown opcode)"; }
			virtual bool op_reads_register(int op) const { return false; }
			virtual size_t op_operand_count(int op) const { return 0; }
			virtual const int *op_operand_form_list(int op) const { return empty_operand_form_list; }
            virtual int get_explicit_interp(int attr, int form) const { return interp::EOL; }
            virtual int get_interp(int attr, int form) const { return interp::EOL; }
            virtual const char *interp_lookup(int interp) const { return "(unknown class)"; }
            virtual std::ostream& print(std::ostream& o) const {
            	o << "Tags:\nAttributes:\nForms:\nEncodings:\nOperators:\nInterpretations:\nAttribute classes:\nForm classes:\n";
                return o;
            }
		};
		
        // specialize this template!
        template <unsigned spec_id> class Extended_By 
        {
        public:
        	typedef struct {} spec;
        };
        
        // specialization for Dwarf 3
        template <> class Extended_By<0U>
        {
        public:
        	typedef empty_def spec;
        };
        
        // forward declarations
        template <unsigned spec_id> class table_def;
        template <unsigned spec_id> 
        std::ostream& operator<<(std::ostream& o, const table_def<spec_id>& table);
        
		template <unsigned spec_id> 
        class table_def : public Extended_By<spec_id>::spec
		{
        public:
        	typedef typename Extended_By<spec_id>::spec Extended;
            static const table_def<spec_id> inst;
		private:	
			// tag table and maps
			const size_t tag_tbl_size; // assigned from *template* constructor
			forward_name_mapping_t *tag_forward_tbl;
			std::map<const char *, int> tag_forward_map;
			inverse_name_mapping_t *tag_inverse_tbl;
			std::map<int, const char *> tag_inverse_map;
		public:
			// tag lookup function			
			virtual const char *tag_lookup(int tag) const 
			{ 
				std::map<int, const char *>::const_iterator found = tag_inverse_map.find(tag);
				if (found != tag_inverse_map.end()) return found->second;
				else return this->Extended::tag_lookup(tag);
			}
			virtual bool tag_is_type(int tag) const;
			virtual bool tag_is_type_chain(int tag) const;            
			virtual bool tag_has_named_children(int tag) const;
		private:
			// attrs
			const size_t attr_tbl_size;
			forward_name_mapping_t *attr_forward_tbl;
			std::map<const char *, int> attr_forward_map;
			inverse_name_mapping_t *attr_inverse_tbl;
			std::map<int, const char *> attr_inverse_map;
		public:
			virtual const char *attr_lookup(int attr) const
			{ 
				std::map<int, const char *>::const_iterator found = attr_inverse_map.find(attr);
				if (found != attr_inverse_map.end()) return found->second;
				else return this->Extended::attr_lookup(attr);
			}
		private:
			attr_class_mapping_t *attr_class_tbl_array;
			std::map<int, const int *> attr_class_tbl;
		public:
            virtual const int *attr_get_classes(int attr) const
            {
				std::map<int, const int *>::const_iterator found = attr_class_tbl.find(attr);
                if (found != attr_class_tbl.end()) return found->second;
                else return this->Extended::attr_get_classes(attr);
            }
			virtual bool attr_describes_location(int attr) const;
		private:
			// forms
			const size_t form_tbl_size;
			forward_name_mapping_t *form_forward_tbl;
			std::map<const char *, int> form_forward_map;
			inverse_name_mapping_t *form_inverse_tbl;
			std::map<int, const char *> form_inverse_map;
		public:
			virtual const char *form_lookup(int form) const
			{ 
				std::map<int, const char *>::const_iterator found = form_inverse_map.find(form);
				if (found != form_inverse_map.end()) return found->second;
				else return this->Extended::form_lookup(form);
			}
		private:
			form_class_mapping_t *form_class_tbl_array;
			std::map<int, const int *> form_class_tbl;
        public:
            virtual const int *form_get_classes(int form) const
            {
				std::map<int, const int *>::const_iterator found = form_class_tbl.find(form);
                if (found != form_class_tbl.end()) return found->second;
                else return this->Extended::form_get_classes(form);
            }
		private:
			// encodings
			const size_t encoding_tbl_size;
			forward_name_mapping_t *encoding_forward_tbl;
			std::map<const char *, int> encoding_forward_map;
			inverse_name_mapping_t *encoding_inverse_tbl;
			std::map<int, const char *> encoding_inverse_map;
		public:
			virtual const char *encoding_lookup(int encoding) const
			{ 
				std::map<int, const char *>::const_iterator found = encoding_inverse_map.find(encoding);
				if (found != encoding_inverse_map.end()) return found->second;
				else return this->Extended::encoding_lookup(encoding);
			}
		private:
			// ops
			const size_t op_tbl_size;
			forward_name_mapping_t *op_forward_tbl;
			std::map<const char *, int> op_forward_map;
			inverse_name_mapping_t *op_inverse_tbl;
			std::map<int, const char *> op_inverse_map;
		public:
			virtual const char *op_lookup(int op) const
			{ 
				std::map<int, const char *>::const_iterator found = op_inverse_map.find(op);
				if (found != op_inverse_map.end()) return found->second;
				else return this->Extended::op_lookup(op);
			}
		private:
			op_operand_forms_mapping_t *op_operand_forms_tbl_array;
			std::map<int, const int *> op_operand_forms_tbl;
		public:
			virtual bool op_reads_register(int op) const 
            { return this->Extended::op_reads_register(op); }
			virtual const int *op_operand_form_list(int op) const
			{ 
				std::map<int, const int *>::const_iterator found = op_operand_forms_tbl.find(op);
				if (found != op_operand_forms_tbl.end()) return found->second;
				else return this->Extended::op_operand_form_list(op);
			}
			virtual size_t op_operand_count(int op) const
			{ 
				int count = 0;
                std::map<int, const int *>::const_iterator found = op_operand_forms_tbl.find(op);
                if (found == op_operand_forms_tbl.end()) return 0U;
				// linear count-up
				for (const int *p_form = found->second; *p_form != 0; p_form++) count++;
				return count;
			}
            // interps
        private:
        	const size_t interp_tbl_size;
        	forward_name_mapping_t *interp_forward_tbl;
            std::map<const char *, int> interp_forward_map;
            inverse_name_mapping_t *interp_inverse_tbl;
            std::map<int, const char *> interp_inverse_map;
            virtual const char *interp_lookup(int interp) const
            {
 				std::map<int, const char *>::const_iterator found = interp_inverse_map.find(interp);
				if (found != interp_inverse_map.end()) return found->second;
				else return this->Extended::interp_lookup(interp);
            }
            virtual int get_explicit_interp(int attr, int form) const;
            virtual int get_interp(int attr, int form) const
            {
			    return get_explicit_interp(attr, form);
            }
 		public: 
			template<	size_t tag_tbl_size,  
						size_t attr_tbl_size,  
						size_t form_tbl_size,  
						size_t encoding_tbl_size, 
						size_t op_tbl_size,
                        size_t interp_tbl_size 
					>	table_def(
                    		TABLE_ARG_DECLS
						) : 
                        Extended(Extended::inst), 
                        // FIXME: ^^^ here we *copy* a ton of stuff, because our Extended is
                        // just the table inst. So, each table inst is copied into any
                        // table insts which extend it. We could fix this by making an 
                        // "indirect table" which just indirects into (rather than copies out)
                        // the underlying inst. This would be fairly trivial... just bother.
                        // *OR* we could allocate the maps outside the class, as we do with
                        // arrays. OR we could make the maps maps of pointers... but this changes
                        // the semantics. Maps of references? Perhaps, but this doesn't save space.
						TABLE_INITS
					{}
        	friend std::ostream& operator<< </* it's a template*/> 
            	(std::ostream& o, const table_def<spec_id>& table);
            virtual std::ostream& print(std::ostream& o) const;
		}; // end template class table_def
        
        void print_symmetric_map_pair(
        	std::ostream& o,
        	const std::map<const char *, int>& forward_map,
            const std::map<int, const char *>& inverse_map);
        
        template<unsigned spec_id> 
        std::ostream& operator<<(std::ostream& o, const table_def<spec_id>& table)
        {
        	return table.print(o);
        }
        
        template<unsigned spec_id>
        std::ostream& table_def<spec_id>::print(std::ostream& o) const
        {
        	o << "Tags: " << std::endl;
            print_symmetric_map_pair(o, this->tag_forward_map, this->tag_inverse_map);

        	o << "Attributes: " << std::endl;
            print_symmetric_map_pair(o, this->attr_forward_map, this->attr_inverse_map);
           
           	o << "Forms: " << std::endl;
            print_symmetric_map_pair(o, this->form_forward_map, this->form_inverse_map);

        	o << "Encodings: " << std::endl;
            print_symmetric_map_pair(o, this->encoding_forward_map, this->encoding_inverse_map);
            
            o << "Opcodes: " << std::endl;
            print_symmetric_map_pair(o, this->op_forward_map, this->op_inverse_map);
            
            o << "Interpretations: " << std::endl;
            print_symmetric_map_pair(o, this->interp_forward_map, this->interp_inverse_map);
                        
            o << "Attribute classes: " << std::endl;
            for (std::map<int, const int *>::const_iterator i = this->attr_class_tbl.begin();
            	i != this->attr_class_tbl.end();
                i++)
            {
            	o << this->attr_lookup(i->first) << ": ";
                for (const int *p = i->second; *p != interp::EOL; p++)                
                {
                	o << this->interp_lookup(*p);
                    if (*(p+1) != interp::EOL) o << ", ";
                }
	            o << std::endl;
            }
            
            o << "Form classes: " << std::endl;
            for (std::map<int, const int *>::const_iterator i = this->form_class_tbl.begin();
            	i != this->form_class_tbl.end();
                i++)
            {
            	o << this->form_lookup(i->first) << ": ";
                for (const int *p = i->second; *p != interp::EOL; p++)                
                {
                	o << this->interp_lookup(*p);
                    if (*(p+1) != interp::EOL) o << ", ";
                }
	            o << std::endl;
            }

			return o;
        }
        
		typedef table_def<0U> dwarf3_def;
        // specialization of template-supplied override
        //extern const dwarf3_def::inst
        extern const abstract_def& DEFAULT_DWARF_SPEC;
        extern const abstract_def& dwarf3;
	}   
}

#define __TABLE_ARGS_PREFIX(prefix) \
prefix tag_forward_tbl, \
prefix tag_inverse_tbl, \
prefix attr_forward_tbl, \
prefix attr_inverse_tbl, \
prefix attr_class_tbl_array, \
prefix form_forward_tbl, \
prefix form_inverse_tbl, \
prefix form_class_tbl_array, \
prefix encoding_forward_tbl, \
prefix encoding_inverse_tbl, \
prefix op_forward_tbl, \
prefix op_inverse_tbl, \
prefix op_operand_forms_tbl_array, \
prefix interp_forward_tbl, \
prefix interp_inverse_tbl

#define TABLE_ARGS_NS(ns) \
	__TABLE_ARGS_PREFIX(ns ::)
    
#define TABLE_ARGS_LOCAL __TABLE_ARGS_PREFIX()

#endif
