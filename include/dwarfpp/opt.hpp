#ifndef DWARFPP_OPT_HPP_
#define DWARFPP_OPT_HPP_

#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>

namespace dwarf
{
	namespace spec
	{
		/* FIXME: this stuff arguably doesn't belong in spec --
		 * rather, it belongs in a util area.
		 * So do the exception types we define, like Not_supportd and No_entry,
		 * arguably, although at least some of those correspond directly to
		 * libdwarf errors, so should perhaps go/stay in lib::. */
	
		using namespace boost; // makes life easier while we have these pasted constructors
		//using boost::optional;
		//using boost::shared_ptr;
		
		/* We use this to encode optional attributes. It's like boost::optional,
		 * but since pointers already model the optional concept, we avoid a double-
		 * indirection (**attr) to get the value of a pointer-valued attribute. */
		// FIXME: don't hardcode boost::shared_ptr here
		template <typename T>
		struct opt : optional<T>
		{
			typedef optional<T> super;
			
			// we'd like to write this:
			//using optional<T>::optional;

			// ... but g++ doesn't support constructor forwarding (as of 4.7)...
			// and std::forward doesn't interact correctly with the copy constructor
			// (which we need to be able to specify separately)...
			
			// ... so repeat boost::optional's constructors inline :-(
    // Creates an optional<T> uninitialized.
    // No-throw
    opt() : super() {}

    // Creates an optional<T> uninitialized.
    // No-throw
    opt( none_t none_ ) : super(none_) {}

    // Creates an optional<T> initialized with 'val'.
    // Can throw if T::T(T const&) does
    opt ( T val ) : super (val) {}

    // Creates an optional<T> initialized with 'val' IFF cond is true, otherwise creates an uninitialized optional.
    // Can throw if T::T(T const&) does
    opt ( bool cond, T val ) : super(cond,val) {}

#ifndef BOOST_OPTIONAL_NO_CONVERTING_COPY_CTOR
    // NOTE: MSVC needs templated versions first

    // Creates a deep copy of another convertible optional<U>
    // Requires a valid conversion from U to T.
    // Can throw if T::T(U const&) does
    template<class U>
    explicit opt ( opt<U> const& rhs )
      :
      super(rhs) {}
//    {
//      if ( rhs.is_initialized() )
//        this->construct(rhs.get());
//    }
#endif

#ifndef BOOST_OPTIONAL_NO_INPLACE_FACTORY_SUPPORT
    // Creates an optional<T> with an expression which can be either
    //  (a) An instance of InPlaceFactory (i.e. in_place(a,b,...,n);
    //  (b) An instance of TypedInPlaceFactory ( i.e. in_place<T>(a,b,...,n);
    //  (c) Any expression implicitely convertible to the single type
    //      of a one-argument T's constructor.
    //  (d*) Weak compilers (BCB) might also resolved Expr as optional<T> and optional<U>
    //       even though explicit overloads are present for these.
    // Depending on the above some T ctor is called.
    // Can throw is the resolved T ctor throws.
    template<class Expr>
    explicit opt ( Expr const& expr ) : super(expr) {}
#endif

//    // Creates a deep copy of another optional<T>
//    // Can throw if T::T(T const&) does
//    optional ( optional const& rhs ) : base( static_cast<base const&>(rhs) ) {}
			

			/* we need our own copy constructor and assignment operator too..
			 * use the "class U" convertibility trick we learnt from boost. */
			//template <class U>
			//opt(const opt<U>& arg) : super(*arg) {}
			
			template <class U>
 			opt<T>& operator=(const opt<U>& arg) { assign(arg); return *this; }
		};
		template <typename T>
		struct opt<shared_ptr<T> > : shared_ptr<T> 
		{
			typedef shared_ptr<T> super;
			
			/* forward the underlying constructor */ 
			//template <typename... Args> 
			//opt(Args&&... args) : shared_ptr<T>(std::forward<Args>(args)...) {}
			
    // Creates an optional<T> uninitialized.
    // No-throw
    opt() : super() {}

    // Creates an optional<T> uninitialized.
    // No-throw
    opt( none_t none_ ) : super(/*none_*/) {}

    // Creates an optional<T> initialized with 'val'.
    // Can throw if T::T(T const&) does
    opt ( shared_ptr<T> val ) : super(val) {}

    // Creates an optional<T> initialized with 'val' IFF cond is true, otherwise creates an uninitialized optional.
    // Can throw if T::T(T const&) does
    opt ( bool cond, shared_ptr<T> val ) : super(cond ? val : shared_ptr<T>()) {}

#ifndef BOOST_OPTIONAL_NO_CONVERTING_COPY_CTOR
    // NOTE: MSVC needs templated versions first

    // Creates a deep copy of another convertible optional<U>
    // Requires a valid conversion from U to T.
    // Can throw if T::T(U const&) does
    template<class U>
    explicit opt ( opt<U> const& rhs )
      :
      super()
    {
      if ( rhs.is_initialized() )
        this->construct(rhs.get());
    }
#endif

#ifndef BOOST_OPTIONAL_NO_INPLACE_FACTORY_SUPPORT
    // Creates an optional<T> with an expression which can be either
    //  (a) An instance of InPlaceFactory (i.e. in_place(a,b,...,n);
    //  (b) An instance of TypedInPlaceFactory ( i.e. in_place<T>(a,b,...,n);
    //  (c) Any expression implicitely convertible to the single type
    //      of a one-argument T's constructor.
    //  (d*) Weak compilers (BCB) might also resolved Expr as optional<T> and optional<U>
    //       even though explicit overloads are present for these.
    // Depending on the above some T ctor is called.
    // Can throw is the resolved T ctor throws.
    template<class Expr>
    explicit opt ( Expr const& expr ) : super(expr,boost::addressof(expr)) {}
#endif
			
			
			
			
			
			
			/* we need a copy constructor and assignment operator too */
			opt(const opt<shared_ptr<T> >& arg) : super((const super&)arg) {}
			
			opt<T>& operator=(const opt<shared_ptr<T> >& arg) 
			{ *((super *) this) = (const super&) arg; return *this; }
		};
		
		/* This does the opposite. We only need it in encap.hpp where we have macros
		 * that want to do attribute_value(*a) for any argument a. Normally the caller
		 * would know not to do *a, but in that macroised generic code, we can't know. */
		template <typename T>
		boost::shared_ptr<T> deref_opt(const opt<shared_ptr<T> >& arg)
		{ return arg; }
		
		template <typename T>
		T deref_opt(const opt<T>& arg)
		{ return *arg; }
		
	}
}

#endif
