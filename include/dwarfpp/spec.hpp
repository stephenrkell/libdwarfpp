/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * spec.hpp: definitions describing DWARF standards and vendor extensions.
 *
 * Copyright (c) 2008--9, Stephen Kell.
 */

#ifndef DWARFPP_SPEC_HPP_
#define DWARFPP_SPEC_HPP_

#include <map>
#include <string>
#include <utility>
#include <cstdlib>
#include <iostream>
#include <cassert>
#include <algorithm>
#include <type_traits>

/* Basic idea of this file: 
 *
 * - every variant of the standard gets its own namespace 
     under dwarf::spec::
 * - each such namespace defines a bunch of maps
 * - we instantiate a table_def using these maps *and*, optionally, an extended def 
 * - these maps only record the increment over the extended def
 * - we build the final abstract_def object 
     as a singleton of a fresh class (so it can get its own code, inlined etc..)
     built CRTP-wise as an extension of a predecessor standard
 * - each such class includes (as static consts) its tables as std::maps
 
 * FIXME: get rid of const methods in abstract_def?
 * Or will that stop us from passing them by ref?
 * I don't think the ref-to-temporary case is important, so no. Get rid!
 */

namespace dwarf
{
	namespace spec
	{
		using std::string;
		extern "C"
		{
			#include "dwarf-onlystd.h"
		}
		struct abstract_def
		{
			virtual const char *tag_lookup(int tag) const = 0;
			virtual int tag_for_name(const char *name) const = 0;
			virtual bool tag_is_type(int tag) const = 0;
			virtual bool tag_is_type_chain(int tag) const = 0;
			virtual bool tag_has_named_children(int tag) const = 0;
			
			virtual const char *attr_lookup(int attr) const = 0;
			virtual int attr_for_name(const char *name) const = 0;
			virtual const int *attr_get_classes(int attr) const = 0;
			virtual bool attr_describes_location(int attr) const = 0;
			
			virtual const char *form_lookup(int form) const = 0;
			virtual int form_for_name(const char *name) const = 0;
			virtual const int *form_get_classes(int form) const = 0;
			
			virtual const char *encoding_lookup(int encoding) const = 0;
			virtual int encoding_for_name(const char *name) const = 0;
			
			virtual const char *op_lookup(int op) const = 0;
			virtual int op_for_name(const char *name) const = 0;
			virtual bool op_reads_register(int op) const = 0;
			virtual size_t op_operand_count(int op) const = 0;
			virtual const int *op_operand_form_list(int op) const = 0;
			
			virtual int get_explicit_interp(int attr, int form) const = 0;
			virtual int get_interp(int attr, int form) const = 0;
			
			virtual const char *interp_lookup(int interp) const = 0;
			
			friend std::ostream& operator<<(std::ostream& o, const abstract_def& a);
			virtual std::ostream& print(std::ostream& o) const = 0;
			
			virtual ~abstract_def() {}
		};
		struct string_comparator
		{
			bool operator()(const char *arg1, const char *arg2) const 
			{
				return string(arg1) < string(arg2);
			}
		};

		using std::cerr;
		
#define DECLARE_MAPS \
			static const std::map<const char *, int, string_comparator> tag_forward_map; \
			static const std::map<int, const char *> tag_inverse_map; \
			static const std::map<const char *, int, string_comparator> form_forward_map; \
			static const std::map<int, const char *> form_inverse_map; \
			static const std::map<const char *, int, string_comparator> attr_forward_map; \
			static const std::map<int, const char *> attr_inverse_map; \
			static const std::map<const char *, int, string_comparator> encoding_forward_map; \
			static const std::map<int, const char *> encoding_inverse_map; \
			static const std::map<const char *, int, string_comparator> op_forward_map; \
			static const std::map<int, const char *> op_inverse_map; \
			static const std::map<const char *, int, string_comparator> interp_forward_map; \
			static const std::map<int, const char *> interp_inverse_map; \
			static const std::map<int, const int *> op_operand_forms_map; \
			static const std::map<int, const int *> attr_class_map; \
			static const std::map<int, const int *> form_class_map; 

#define DECLARE_BOILERPLATE(typename) \
			static typename inst; \
			std::ostream& print(std::ostream& o) { \
				/*return print< typename >(o); */ return o; \
			}

#define DECLARE_LOCAL_PREDS \
			bool local_tag_is_type(int tag) const; \
			bool local_tag_is_type_chain(int tag) const; \
			bool local_tag_has_named_children(int tag) const; \
			bool local_attr_describes_location(int attr) const; \
			bool local_op_reads_register(int op) const;

				
		template <class DefWithMaps>
		std::ostream& print(std::ostream& o);

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
				exprloc, 
				block_as_dwarf_expr = 0x20, 
				constant_to_make_location_expr, 
				FLAGS = 0x7f000000,
				UNSIGNED = 0x01000000, 
				SIGNED = 0x02000000
			};
		};
		
		class empty_def_t : public virtual abstract_def
		{
			static const int empty_operand_form_list[];
			static const int empty_class_list[];
			
		public:
			DECLARE_MAPS
			DECLARE_BOILERPLATE(empty_def_t)
			
			/* Override all the lookups to print a warning 
			 * if we hit them, because it means a lookup
			 * has failed. We also don't bother to consult
			 * our empty maps. */
			virtual const char *tag_lookup(int tag) const 
			{ cerr << "Saw unknown tag 0x" << std::hex << tag << std::dec << std::endl;
				return "(unknown tag)"; }
			virtual int tag_for_name(const char *name) const
			{ 
				cerr << "Saw unknown tag name "<< name << std::endl;
				return -1;
			}
			virtual const char *attr_lookup(int attr) const 
			{
				cerr << "Saw unknown attr 0x" << std::hex << attr << std::dec << std::endl;
				return "(unknown attribute)"; 
			}
			virtual int attr_for_name(const char *name) const
			{ 
				cerr << "Saw unknown attr name "<< name << std::endl;
				return -1;
			}
			virtual const char *form_lookup(int form) const 
			{
				cerr << "Saw unknown form 0x" << std::hex << form << std::dec << std::endl;
				return "(unknown form)"; 
			}
			virtual int form_for_name(const char *name) const
			{ 
				cerr << "Saw unknown form name "<< name << std::endl;
				return -1;
			}
			virtual const char *encoding_lookup(int encoding) const 
			{ 
				cerr << "Saw unknown encoding 0x" << std::hex << encoding << std::dec << std::endl;
				return "(unknown encoding)";
			}
			virtual int encoding_for_name(const char *name) const
			{ 
				cerr << "Saw unknown encoding name "<< name << std::endl;
				return -1;
			}
			virtual const char *op_lookup(int op) const 
			{ 
				cerr << "Saw unknown opcode 0x" << std::hex << op << std::dec << std::endl;
				return "(unknown opcode)"; 
			}
			virtual int op_for_name(const char *name) const
			{ 
				cerr << "Saw unknown op name "<< name << std::endl;
				return -1;
			}
			virtual const char *interp_lookup(int interp) const 
			{ return "(unknown class)"; }
			
			/* Define our preds trivially. */
			virtual bool tag_is_type(int tag) const 
			{ return false; }
			virtual bool tag_is_type_chain(int tag) const 
			{ return false; }
			virtual bool tag_has_named_children(int tag) const 
			{ return false; }
			virtual bool attr_describes_location(int attr) const 
			{ return false; }
			virtual bool op_reads_register(int op) const { return false; }

			/* Define our empty lists. */
			virtual const int *attr_get_classes(int attr) const 
			{ return empty_class_list; }
			virtual size_t op_operand_count(int op) const { return 0; }
			virtual const int *op_operand_form_list(int op) const 
			{ return empty_operand_form_list; }
			virtual const int *form_get_classes(int form) const 
			{ return empty_class_list; }

			virtual int get_explicit_interp(int attr, int form) const { return interp::EOL; }
			virtual int get_interp(int attr, int form) const { return interp::EOL; }

			virtual std::ostream& print(std::ostream& o) const 
			{
				o << "Tags:\nAttributes:\nForms:\nEncodings:\nOperators:\nInterpretations:\nAttribute classes:\nForm classes:\n";
				return o;
			}
		};
		extern empty_def_t empty_def;
		
		template <typename Extended, typename Extending>
		struct extension_of : public virtual abstract_def
		{
			// try Extending's maps, else delegate to Extended's lookup method
			/* Our maps use const char * but our methods take const string&,
			 * so we want to allow convertibility between these. This means that
			 * K (method signature) and MappedK (map type parameters) 
			 * are not necessarily the same. */
			template <typename K, typename V, typename Cmp>
			V
			map_union_lookup(
				const std::map<K, V, Cmp>& m1, 
				V (Extended::*method)(K) const, 
				const K& k) const
			{
				typename std::map<
					K,
					V
				>::const_iterator found1 = m1.find(k);
				if (found1 != m1.end())
				{
					return found1->second;
				} else return (Extended::inst.*method)(k);
			}
			
			const char *tag_lookup(int tag) const 
			{
				return map_union_lookup(Extending::tag_inverse_map, &Extended::tag_lookup, tag);
			}
			int tag_for_name(const char *name) const
			{
				return map_union_lookup(Extending::tag_forward_map, &Extended::tag_for_name, name);
			}
			const char *attr_lookup(int attr) const 
			{
				return map_union_lookup(Extending::attr_inverse_map, &Extended::attr_lookup, attr);
			}
			int attr_for_name(const char *name) const
			{
				return map_union_lookup(Extending::attr_forward_map, &Extended::attr_for_name, name);
			}
			const char *form_lookup(int form) const 
			{
				return map_union_lookup(Extending::form_inverse_map, &Extended::form_lookup, form); 
			}
			int form_for_name(const char *name) const
			{
				return map_union_lookup(Extending::form_forward_map, &Extended::form_for_name, name);
			}
			const char *encoding_lookup(int encoding) const 
			{
				return map_union_lookup(Extending::encoding_inverse_map, &Extended::encoding_lookup, encoding); 
			}
			int encoding_for_name(const char *name) const
			{
				return map_union_lookup(Extending::encoding_forward_map, &Extended::encoding_for_name, name);
			}
			const char *op_lookup(int op) const 
			{
				return map_union_lookup(Extending::op_inverse_map, &Extended::op_lookup, op); 
			}
			int op_for_name(const char *name) const
			{
				return map_union_lookup(Extending::op_forward_map, &Extended::op_for_name, name);
			}
			const char *interp_lookup(int interp) const 
			{
				return map_union_lookup(Extending::interp_inverse_map, &Extended::interp_lookup, interp); 
			}
			
			// union Extending's pred with Extended's
			template <typename Arg>
			bool 
			member_pred_union(bool(Extending::* pred1)(Arg) const, bool(Extended::* pred2)(Arg) const, const Arg& arg) const
			{
				return (Extending::inst.*pred1)(arg)
				|| (Extended::inst.*pred2)(arg);
			}
			
			bool tag_is_type(int tag) const 
			{
				return member_pred_union(&Extending::local_tag_is_type, &Extended::tag_is_type, tag);
			}
			bool tag_is_type_chain(int tag) const 
			{
				return member_pred_union(&Extending::local_tag_is_type_chain, &Extended::tag_is_type_chain, tag);
			}
			bool tag_has_named_children(int tag) const 
			{
				return member_pred_union(&Extending::local_tag_has_named_children, &Extended::tag_has_named_children, tag);
			}
			bool attr_describes_location(int attr) const 
			{
				return member_pred_union(
					&Extending::local_attr_describes_location, 
					&Extended::attr_describes_location, 
					attr);
			}
			bool op_reads_register(int op) const 
			{ 
				return member_pred_union(&Extending::local_op_reads_register, &Extended::op_reads_register, op);
			}
			
			// lists
			const int *attr_get_classes(int attr) const 
			{
				return map_union_lookup(Extending::attr_class_map, &Extended::attr_get_classes, attr); 
			}
			const int *form_get_classes(int form) const 
			{
				return map_union_lookup(Extending::form_class_map, &Extended::form_get_classes, form); 
			}
			size_t op_operand_count(int op) const
			{
				int count = 0;
				std::map<int, const int *>::const_iterator found = Extending::inst.op_operand_forms_map.find(op);
				if (found != Extending::inst.op_operand_forms_map.end())
				{
					// linear count-up
					for (const int *p_form = found->second; *p_form != 0; p_form++) count++;
					return count;		
				} else return Extended::inst.op_operand_count(op);
			}
			const int *op_operand_form_list(int op) const 
			{
				return map_union_lookup(Extending::op_operand_forms_map, &Extended::op_operand_form_list, op); 
			}

			int get_explicit_interp(int attr, int form) const
			{
				return explicit_interp(*const_cast<abstract_def*>(static_cast<const abstract_def*>(this)), attr, attr_get_classes(attr), form, form_get_classes(form));
			}
			int get_interp(int attr, int form) const 
			{
				return get_explicit_interp(attr, form);
			}

			std::ostream& print(std::ostream& o) const 
			{
				o << "Tags:\nAttributes:\nForms:\nEncodings:\nOperators:\nInterpretations:\nAttribute classes:\nForm classes:\n";
				return o;
			}
		};
		
		/* define DWARF4 as an extension of the empty def */
		class dwarf4_t : public extension_of<empty_def_t, dwarf4_t >//, 
		{
		public:
			/* Q. How do we populate the maps? 
			 * A. They're static const, so we define them out-of-line. */
			DECLARE_MAPS
			DECLARE_BOILERPLATE(dwarf4_t)
			
			/* Q. How do we define our preds so that they don't hide
			      the ones we inherit (that call ours)?
			 * A. Prefix them with "local". */ 
			DECLARE_LOCAL_PREDS
		
			/* We also need to hack the get_interp logic a little, 
			 * so declare that too. */
			int get_interp(int attr, int form) const;
		};
		extern dwarf4_t& dwarf4;
		
		/* define the 'head' DWARF as an extension of DWARF4. */
		class dwarf_head_t: public extension_of<dwarf4_t, dwarf_head_t >, 
			public virtual abstract_def
		{
		public:
			DECLARE_MAPS
			DECLARE_BOILERPLATE(dwarf_head_t)
			DECLARE_LOCAL_PREDS
		};
		extern dwarf_head_t dwarf_head;
		
		namespace gnu
		{
			#include "dwarf-ext-GNU.h"
			
			class dwarf4_plus_gnu_t : public extension_of<dwarf4_t, dwarf4_plus_gnu_t >, 
				public virtual abstract_def
			{
			public:
				DECLARE_MAPS
				DECLARE_BOILERPLATE(dwarf4_plus_gnu_t)
				DECLARE_LOCAL_PREDS;
			};
			extern dwarf4_plus_gnu_t& dwarf4_plus_gnu;
		}

		using std::cerr;
		
		// standalone helper
		int explicit_interp(abstract_def& def, int attr, const int *attr_possible_classes, int form, const int *form_possible_classes);
	
		void print_symmetric_map_pair(
			std::ostream& o,
			const std::map<const char *, int>& forward_map,
			const std::map<int, const char *>& inverse_map);
		
		template <class DefWithMaps>
		std::ostream& print(std::ostream& o) 
		{
			o << "Tags: " << std::endl;
			print_symmetric_map_pair(o, DefWithMaps::tag_forward_map, DefWithMaps::tag_inverse_map);

			o << "Attributes: " << std::endl;
			print_symmetric_map_pair(o, DefWithMaps::attr_forward_map, DefWithMaps::attr_inverse_map);
		
			o << "Forms: " << std::endl;
			print_symmetric_map_pair(o, DefWithMaps::form_forward_map, DefWithMaps::form_inverse_map);

			o << "Encodings: " << std::endl;
			print_symmetric_map_pair(o, DefWithMaps::encoding_forward_map, DefWithMaps::encoding_inverse_map);
			
			o << "Opcodes: " << std::endl;
			print_symmetric_map_pair(o, DefWithMaps::op_forward_map, DefWithMaps::op_inverse_map);
			
			o << "Interpretations: " << std::endl;
			print_symmetric_map_pair(o, DefWithMaps::interp_forward_map, DefWithMaps::interp_inverse_map);
						
			o << "Attribute classes: " << std::endl;
			for (std::map<int, const int *>::const_iterator i = DefWithMaps::attr_class_tbl.begin();
				i != DefWithMaps::attr_class_tbl.end();
				i++)
			{
				o << DefWithMaps::attr_lookup(i->first) << ": ";
				for (const int *p = i->second; *p != interp::EOL; p++)				
				{
					o << DefWithMaps::interp_lookup(*p && ~interp::FLAGS);
					if (*(p+1) != interp::EOL) o << ", ";
				}
				o << std::endl;
			}
			
			o << "Form classes: " << std::endl;
			for (std::map<int, const int *>::const_iterator i = DefWithMaps::form_class_tbl.begin();
				i != DefWithMaps::form_class_tbl.end();
				i++)
			{
				o << DefWithMaps::form_lookup(i->first) << ": ";
				for (const int *p = i->second; *p != interp::EOL; p++)				
				{
					o << DefWithMaps::interp_lookup(*p && ~interp::FLAGS);
					if (*(p+1) != interp::EOL) o << ", ";
				}
				o << std::endl;
			}

			return o;
		}

		extern abstract_def& DEFAULT_DWARF_SPEC;
		extern abstract_def& dwarf_current; // HACK
		extern dwarf4_t& dwarf4;
	}
}

#endif
