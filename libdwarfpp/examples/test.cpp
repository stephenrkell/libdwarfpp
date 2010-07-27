#include <utility>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/graph_concepts.hpp>

namespace mine { struct Gr {}; }
namespace boost
{
    template<>
	struct graph_traits<mine::Gr> 
    {
        typedef void *vertex_descriptor;
        typedef std::pair<void*, void*> edge_descriptor;
		typedef void** vertex_iterator;
        typedef directed_tag directed_category;
        typedef allow_parallel_edge_tag edge_parallel_category;
        struct traversal_tag :
          public virtual vertex_list_graph_tag,
          public virtual incidence_graph_tag {};
        typedef traversal_tag traversal_category;
        typedef size_t vertices_size_type;
        typedef size_t edges_size_type;
        typedef size_t degree_size_type;
    	typedef std::pair<void*, void*> *out_edge_iterator;
    };
                    
} // end namespace boost
namespace mine {
    typedef std::pair<boost::graph_traits<Gr>::out_edge_iterator,
		            boost::graph_traits<Gr>::out_edge_iterator>  edge_iter_pair;
    typedef std::pair<boost::graph_traits<Gr>::vertex_iterator,
		            boost::graph_traits<Gr>::vertex_iterator>  vertex_iter_pair;

    edge_iter_pair out_edges(boost::graph_traits<Gr>::vertex_descriptor u, const Gr& g);
    
	boost::graph_traits<Gr>::degree_size_type out_degree(
    	boost::graph_traits<Gr>::vertex_descriptor u,
        const Gr& g);
    boost::graph_traits<Gr>::vertex_descriptor source(
        boost::graph_traits<Gr>::edge_descriptor e,
        const Gr& g);
    boost::graph_traits<Gr>::vertex_descriptor target(
        boost::graph_traits<Gr>::edge_descriptor e,
        const Gr& g);
	vertex_iter_pair vertices(const Gr& g);
    boost::graph_traits<Gr>::vertices_size_type num_vertices(const Gr& g);
}

using namespace mine;

int main()
{
    mine::Gr g;
    mine::out_edges(*mine::vertices(g).first, g);
    boost::function_requires< boost::IncidenceGraphConcept<mine::Gr> >();
    boost::function_requires< boost::VertexListGraphConcept<mine::Gr> >();    
}
