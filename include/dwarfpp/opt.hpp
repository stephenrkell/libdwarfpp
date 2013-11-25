#ifndef DWARFPP_OPT_HPP_
#define DWARFPP_OPT_HPP_

#include <boost/optional.hpp>
#include <memory>

namespace dwarf
{
	namespace core 
	{
		struct iterator_base; 
		struct type_die;
		
		template <typename DerefAs> 
		struct iterator_df;
	}
	namespace spec
	{
		/* FIXME: this stuff arguably doesn't belong in spec, but in a util area. */
		 
		/* (So do the exception types we define, like Not_supported and No_entry,
		 * arguably. FIXME: move those.) */
		
		using std::shared_ptr; 
		using boost::optional;
		
		/* We use this to encode optional attributes. It's like boost::optional,
		 * but since pointers already model the optional concept, we avoid a double-
		 * indirection (**attr) to get the value of a pointer-valued attribute. */
		// FIXME: don't hardcode std::shared_ptr here
		template <typename T, typename Enabler = void>
		struct opt : optional<T>
		{
			typedef optional<T> super;
			
			// needs g++ 4.8+! or some other C++11-compliant compiler
			using optional<T>::optional;
		};
		/* END the non-specialised opt<> case. */
		
		/* BEGIN the opt<> case specialized for shared_ptr. */
		template <typename T>
		struct opt<shared_ptr<T> > : shared_ptr<T> 
		{
			typedef shared_ptr<T> super;
			
			/* forward the underlying constructors */ 
			using shared_ptr<T>::shared_ptr;
		};

		/* Similarly, we need one for iterators, i.e. things which extend 
		 * iterator_base. Can we express this specialization? It seems that
		 * std::enable_if can do this. This is why we need the extra Enabler
		 * template argument, though. (We could try the more direct approach,
		 * but the compiler complains that the specialization doesn't name the
		 * template argument T. I'm not convinced that this complaint is valid.) */
		template <typename T>
		struct opt< T, typename std::enable_if<std::is_base_of<core::iterator_base, T>::value >::type >
		 : T
		{
			typedef T super;

			/* forward the underlying constructors */ 
			using T::T;
		};

		/* These do the opposite: unspecialise, to reinstate the double indirection. 
		 * We only need them in encap.hpp where we have macros
		 * that want to do attribute_value(*a) for any argument a. Normally the caller
		 * would know not to do *a, but in that macroised generic code, we can't know. 
		 * So we have the code do *deref_opt(), which no-ops the unwanted deref. */
		template <typename T>
		std::shared_ptr<T> deref_opt(const opt<shared_ptr<T> >& arg)
		{ return arg; }

		template <typename T>
		T deref_opt(const opt<T>& arg)
		{ return *arg; }

		template <typename T>
		T
		deref_opt(const opt< T, typename std::enable_if<std::is_base_of<core::iterator_base, T>::value, T >::type >& arg)
		{
			return arg;
		}
	} /* end namespace spec */
}

#endif
