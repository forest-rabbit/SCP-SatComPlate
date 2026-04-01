#ifndef BETWEENNESS_CENTRALITY_H
#define BETWEENNESS_CENTRALITY_H

#include <iostream>
#include <vector>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/betweenness_centrality.hpp>
#include <boost/graph/iteration_macros.hpp>

using namespace boost;
using namespace std;

// 定义图类型
typedef adjacency_list<vecS, vecS, undirectedS> Graph;
typedef graph_traits<Graph>::vertex_descriptor Vertex;

// 从邻接表创建图
Graph CreateGraph(const vector<vector<int>>& adjacencyList);

// 计算并打印介数中心度
vector<double> CalculateBetweennessCentrality(const vector<vector<int>>& adjacencyList);

#endif
