#include "betweenness-centrality.h"

Graph CreateGraph(const vector<vector<int>>& adjacencyList) {
    Graph g(adjacencyList.size());
    for (size_t u = 0; u < adjacencyList.size(); ++u) {
        for (size_t v : adjacencyList[u]) {
            add_edge(u, v, g);
        }
    }
    return g;
}

vector<double> CalculateBetweennessCentrality(const vector<vector<int>>& adjacencyList) {
    Graph g = CreateGraph(adjacencyList);
    vector<double> centrality(num_vertices(g), 0.0);

    brandes_betweenness_centrality(g,
        centrality_map(
            make_iterator_property_map(centrality.begin(), get(vertex_index, g))
        )
    );
    return centrality;
    // 打印每个顶点的介数中心度
    // for (size_t i = 0; i < centrality.size(); ++i) {
    //     std::cout << "Node " << i << " Betweenness Centrality: " << centrality[i] << std::endl;
    // }
}