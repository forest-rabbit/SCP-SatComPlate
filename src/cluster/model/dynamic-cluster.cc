#include "dynamic-cluster.h"
#include <cmath>

NS_LOG_COMPONENT_DEFINE ("SatCluster");

TypeId 
SatCluster::GetTypeId(void)
{
    static ns3::TypeId tid = ns3::TypeId("SatCluster")
                                .SetParent(ns3::Object::GetTypeId())
                                .SetGroupName("SatCluster");
    return tid;
}

  TwoLayerConfig
  SatCluster::MakeDefaultTwoLayerConfig()
  {
    TwoLayerConfig cfg{};
    cfg.alpha = 0.85;
    cfg.beta = 0.15;
    cfg.gamma = 0.0;          // 默认不对目标簇数添加惩罚
    cfg.target_k = 0;         // 0 表示不强制目标簇数
    cfg.k_min = 4;
    cfg.k_max = 8;
    cfg.max_outer_iter = 25;
    cfg.convergence_threshold = 1e-3;
    cfg.D_max = 6;
    cfg.M_min = 3;
    cfg.tabu_size = 5;
    cfg.stall_limit = 5;
    return cfg;
  }


// ========== 状态管理 ==========
ClusterState 
SatCluster::BackupState()
{
    return ClusterState(m_cluster, node_to_sat);
}

void 
SatCluster::RestoreState(const ClusterState& state)
{
    m_cluster = state.clusters;
    node_to_sat = state.node_mapping;
}


// ========== 代价评估核心函数 ==========
CostState
SatCluster::EvaluateCost(NodeContainer nodes, const TwoLayerConfig& config)
{
    CostState state;
    state.adj = AdjacenyList(nodes);
    state.metrics = ComputeClusterMetrics(nodes, state.adj);
    state.J_delay = ComputeDelayTarget(state.metrics, nodes, state.adj);
    state.J_stable = ComputeStabilityTarget(state.metrics, state.adj);
    state.is_connected = CheckInterClusterConnectivity(state.adj);
    
    if (!state.is_connected)
    {
      state.cost = std::numeric_limits<double>::max(); // 直接拒绝不连通解
      return state;
    }

    state.cost = config.alpha * state.J_delay + config.beta * state.J_stable;

    // 目标簇数偏离惩罚（可选）
    // if (config.target_k > 0)
    // {
    //   state.cost += config.gamma * std::abs(static_cast<int>(m_cluster.size()) - static_cast<int>(config.target_k));
    // }
    
    return state;
}

vector<vector<int>> 
SatCluster::AdjacenyList(NodeContainer node)
{
  vector<vector<int>> adj_list(node.GetN());
  //uint32_t index = 0; // 偏移量，定义为第一个卫星节点ID相对于0的偏移量
  for(uint32_t i = 0; i < node.GetN(); i++)
  {
    Ptr<Node> m_node = node.Get(i);
    if(i == 0) begin_id = node.Get(i)->GetId();  
    // cout << "node:" << i << "\tnodeID:" << node.Get(i)->GetId() << "\tdeviceSize:" << m_node->GetNDevices() << endl;
    for(uint32_t j = 0; j < m_node->GetNDevices(); j++)
    {
      // if(j>=5) break;
      
      // cout << "node:" << i << "\tdevice:" << j << endl;
      Ptr<NetDevice> dev = m_node->GetDevice(j);
      Ptr<PointToPointNetDevice> p2p_dev = DynamicCast<PointToPointNetDevice>(dev);
      // if(p2p_dev == nullptr) cout << "node:" << i << "\tnodeID:" << node.Get(i)->GetId() << " nullptr" << endl;
      // else if(!p2p_dev->IsLinkUp()) cout << "node:" << i << "\tnodeID:" << node.Get(i)->GetId() << " link down" << endl;
      if(p2p_dev != nullptr && p2p_dev->IsLinkUp())//add connect flag
      {
        Ptr<Channel> channel = p2p_dev->GetChannel();
        // cout << "node:" << i << "\tnodeID:" << node.Get(i)->GetId() << "\tchannelsize:" << channel->GetNDevices() << endl;
        for(uint32_t k = 0; k < channel->GetNDevices(); k++)
        {
          Ptr<NetDevice> adj_device = channel->GetDevice(k);
          if( adj_device != dev)
          {
            Ptr<Node> adj_node = adj_device->GetNode();
            if(adj_node->GetId() < begin_id) continue; // 避免地面节点
            adj_list[node.Get(i)->GetId() - begin_id].push_back(adj_node->GetId() - begin_id);// 
            // cout << "cluster             adj_list[" << node.Get(i)->GetId() - index << "]=" << adj_node->GetId() - index << endl;
          }
        }
      }
    }
  }
  return adj_list;
}

void 
SatCluster::ClusterSize(const vector<vector<int>>& adj, uint32_t sat_node)
{
  //s.t. 2 <= D/n <= d --d is diameter every cluster, n is the number of cluster, 2 is the lowest cluster diameter, D is network diameter
  uint32_t n = 0; //the number of cluster
  uint32_t aver_diameter = 0; //the  aver diameter each cluster
  vector<bool>visit(adj.size(),false);
  std::queue<int> queue;
	visit[sat_node] = true;
	queue.push(sat_node);
	uint32_t net_diameter = -1;//network diameter
	while(!queue.empty())
	{
		uint32_t size = queue.size();
		for(uint32_t i = 0; i < size; ++i)
		{
			uint32_t cur_node = queue.front();
			queue.pop();
			for(uint32_t j = 0; j < adj[cur_node].size(); j++)
			{
				if(!visit[adj[cur_node][j]])
				{
					visit[adj[cur_node][j]] = true;
					queue.push(adj[cur_node][j]);
				}
			}
		}
		net_diameter++;
	}
  // cout<<net_diameter<<endl;
  n = (uint32_t) net_diameter/2;
  aver_diameter = (uint32_t) adj.size() / n;
  Naver = aver_diameter;
  Nmax = (uint32_t)aver_diameter*1.2;
  Nmin = (uint32_t)aver_diameter*0.8;
}

void
SatCluster::bfs(const vector<vector<int>>& adj, const int& start, vector<bool>& visit, const int& depth, vector<vector<int>>& cluster)
{
  //设定最多搜寻节点个数
  uint32_t threshold = 0;//簇内节点最多数目--Nmax 目前设置搜寻2条以内的节点即最多是13个节点，当网络规模变大可能13为一个簇的节点个数较小，因此需要进行簇合并(修正)
  if(Naver <= 13)
  {
    threshold = Naver;
  }
  else
  {
    threshold = 13;
  }
  bool flag = false;
  int cur_depth = 0;
  vector<int> temp;
  std::queue<int> queue;
  visit[start] = true;
  queue.push(start);
  temp.push_back(start);
  while(!queue.empty() && cur_depth < depth)
  {
    int size = queue.size();
    for(int i = 0; i < size; i++)
    {
      int cur_node = queue.front();
      queue.pop();
      for(uint32_t j = 0; j < adj[cur_node].size(); j++)
      {
        if(!visit[adj[cur_node][j]])
        {
          if(temp.size()>= threshold)
          {
            flag = true;
            break;
          }
          visit[adj[cur_node][j]] = true;
          queue.push(adj[cur_node][j]);
          temp.push_back(adj[cur_node][j]);
        }
      }
      if(flag == true)
      {
        cur_depth = depth;
        break;
      }
      if(i == size -1)
      {
        cur_depth += 1;
      }
    }
  }
  cluster.push_back(temp);
}

pair<int,int>
SatCluster::ConnectivtyMaxDegree(const vector<vector<int>>& adj, vector<int>& a)
{
  int max_degree = -1;
  int max_node = 0;
  std::unordered_set<int> node_hash(a.begin(), a.end());// hash--fast
  for(uint32_t i = 0; i < a.size(); i++)
  {
    int degree = 0;
    for(uint32_t j = 0; j < adj[a[i]].size(); j++)
    {
      if(node_hash.find(adj[a[i]][j]) != node_hash.end())
      {
        degree++;
      }
    }
    if(degree > max_degree)
    {
      max_degree = degree;
      max_node = a[i];
    }
  }
  return {max_node, max_degree};
}

std::unordered_set<uint32_t>
SatCluster::MinDegree(const vector<vector<int>>& adj, vector<Satellite>& sat)
{
  int max_degree = 100;
  //int min_node = 0;
  vector<int> a;
  std::unordered_set<uint32_t> min_nodes;
  for(auto& it : sat)
  {
    a.push_back(it.id);
  }
  std::unordered_set<int> node_hash(a.begin(), a.end());// hash--fast
  for(uint32_t i = 0; i < a.size(); i++)
  {
    int degree = 0;
    for(uint32_t j = 0; j < adj[a[i]].size(); j++)
    {
      if(node_hash.find(adj[a[i]][j]) != node_hash.end())
      {
        degree++;
      }
    }
    if(degree < max_degree)
    {
      max_degree = degree;
      //min_node = a[i];
      min_nodes.clear();
      min_nodes.insert(a[i]);
    }
    else if(degree == max_degree)
    {
      min_nodes.insert(a[i]);
    }
  }
  return min_nodes;
}

uint32_t
SatCluster::ConnectivityMinDegree(const vector<int>& a, const vector<vector<int>>& adj, const vector<bool>& visit)
{
  int min_degree = 100;
  int min_node = 0;
  std::unordered_set<int> node_hash(a.begin(), a.end());// hash--fast
  for(uint32_t i = 0; i < a.size(); i++)
  {
    int degree = 0;
    for(uint32_t j = 0; j < adj[a[i]].size(); j++)
    {
      if(node_hash.find(adj[a[i]][j]) != node_hash.end() && visit[adj[a[i]][j]] == false)
      {
        degree++;
      }
    }
    if(degree < min_degree)
    {
      min_degree = degree;
      min_node = a[i];
    }
  }
  return min_node;
}


uint32_t 
SatCluster::GetDegree(Ptr<Node> node, uint32_t id)
{
  uint32_t degree = 0;
  for(uint32_t j = 1; j < node->GetNDevices(); j++) //consider openflow netdevice  
  {
    if(j>=5) break;
    Ptr<NetDevice> dev = node->GetDevice(j);
    Ptr<PointToPointNetDevice> p2p_dev = DynamicCast<PointToPointNetDevice>(dev);
    if(p2p_dev != nullptr && p2p_dev->IsLinkUp())//add connect flag
    {
      Ptr<Channel> channel = p2p_dev->GetChannel();
      for(uint32_t k = 0; k < channel->GetNDevices(); k++)
      {
        Ptr<NetDevice> adj_device = channel->GetDevice(k);
        if(adj_device != dev)
        {
          Ptr<Node> adj_node = adj_device->GetNode();
          Satellite sat = node_to_sat[adj_node];
          if(sat.cluster_index == id) degree++;
        }
      }
    }
  }
  return degree;
}

vector<Satellite> 
SatCluster::OptionalNode(Cluster &cluster, NodeContainer& nodes)
{
  uint32_t n = cluster.satellites.size();
  vector<Satellite> Sats;
  for(uint32_t i = 0; i < n; ++i)
  {
    Satellite sat = cluster.satellites[i];
    uint32_t id = sat.cluster_index;
    Ptr<Node> node = nodes.Get(sat.id);
    uint32_t degree = GetDegree(node, id);
    if(degree == 1 || degree == 0) Sats.push_back(sat); //add the node with connectivity degree 1 可能存在孤立节点
  }
  //if(Sats.empty()) std::cout << "未能在本簇中找到可以转移的节点" << "\n";
  return Sats;
}

void 
SatCluster::InitialCluster(NodeContainer node)
{
  m_cluster.clear();//清除分簇避免叠加
  int node_num = node.GetN();
  int depth = 2; // two hop 
  vector<vector<int>> cluster; // Initial cluster array
  vector<vector<int>> adj_list = AdjacenyList(node); // Initial adjaceny list
  vector<bool> visit(node_num, false); // Initial visit array
  vector<int> u_visit(node_num);// Initial without visit array
  vector<pair<int,int>> cluster_head;// Initial CH 
  // /*******打印邻表********************/
  // for(uint32_t i = 0; i < adj_list.size(); i++)
  // {
  //     cout<<"node: "<<i+7<<" "<<"adj: ";
  //     for(uint32_t j = 0; j < adj_list[i].size(); j++)
  //     {
  //         cout<<adj_list[i][j]+7<<" ";
  //     }
  //     cout<<endl;
  // }
  /*********************************/
  //ClusterSize(adj_list, 0);
  for(int i = 0; i < node_num; i++)
  {
    u_visit.push_back(i);
  }
  while (u_visit.size()) 
  {
    uint32_t start =  ConnectivityMinDegree(u_visit, adj_list, visit);
    // srand(static_cast<unsigned>(time(0))); // Initial random seed
    // int random_index = rand() % u_visit.size(); // Random select a index without visiting
    // int start = u_visit[random_index];
    //cout<<"the start node: " <<start<<endl;
    bfs(adj_list, start, visit, depth, cluster);
    u_visit.clear();
    for(int i = 0 ; i < node_num; i++)
    {
      if(!visit[i])
      {
        u_visit.push_back(i);
      }
    } 
  }
  // Select the initial CH
  for(uint32_t i = 0; i < cluster.size(); i++)
  {
    pair<int,int> result = ConnectivtyMaxDegree(adj_list, cluster[i]);
    cluster_head.push_back(result);
  }
  // Output the initial cluster result--Initial struct Satellite and Cluster
  // cout<<"******* 1.The initial cluster phase *******"<<endl;
  int tt = 0;
  for(auto it : cluster)
  {
    Cluster clu;
    vector<Satellite> satellites;
    // cout<<"cluster "<<tt+1<<":"<<" ";
    for(int t : it)
    {
      Ptr<Node> node_id = node.Get(t);
      Satellite sat;
      sat.cluster_index = tt;
      sat.id = t;

      sat.max_utilization = 0.0;
      satellites.push_back(sat); // only include the cluster of the satellite node
      m_satellite.push_back(sat); // include all the satellite node
      node_to_sat[node_id] = sat; // in order to merge cluster 
      // cout<<t+7<<" ";
    }
    // cout<<endl;
    //cout<<" CH: "<<cluster_head[tt].first<<" degree: "<<cluster_head[tt].second<<endl;

    clu.fitness = 0.0;
    clu.satellites = satellites;
    clu.cluster_head = cluster_head[tt].first;
    clu.candidate_head = 0;//初始化候选簇头
    m_cluster.push_back(clu);
    tt++;
  }
  // merge 
  MergeCluster(m_cluster, node, adj_list);
  for(int k = m_cluster.size()-1; k >= 0; --k)
  {
    if(m_cluster[k].satellites.size()==0)
    {
      m_cluster.erase(m_cluster.begin()+k);
    }
  }

  // cout<<"Min: "<<Nmin<<" Max: "<<Nmax<<endl;
  for(uint32_t k = 0; k!=m_cluster.size(); k++)
  {
    // cout<<"cluster "<<k+1<<":"<<" ";
    for(auto& sat : m_cluster[k].satellites)
    {
      sat.cluster_index = k; // update cluster_index
      Ptr<Node> node_id = node.Get(sat.id);
      node_to_sat[node_id] = sat;// !!!!!
      // cout<<sat.id+7<<" ";
    }
    // cout<<endl;
    // cout<<" CH: "<<m_cluster[k].cluster_head<<endl;
    //cout << "簇的连通性为：" << IsFullConnectivity(m_cluster[k], adj_list) << "\n";
  }

  // output format: vector<NodeContainer>a
  // vector<NodeContainer>cluster_nodes;
  // for(const auto& i : m_cluster)
  // {
  //   NodeContainer cluster_node;
  //   Ptr<Node> head_node = node.Get(i.cluster_head);
  //   cluster_node.Add(head_node);
  //   for(const auto& j : i.satellites)
  //   {
  //     if(j.id != i.cluster_head)
  //     {
  //       Ptr<Node> member_node = node.Get(j.id);
  //       cluster_node.Add(member_node);
  //     }
  //   }
  //   cluster_nodes.push_back(cluster_node);
  // }
  // return cluster_nodes;
}


void 
SatCluster::MergeCluster(vector<Cluster>& clusters, NodeContainer node, const vector<vector<int>>& adj)
{
  //vector<Cluster> clusters = cluster;

  uint32_t min_threshold = 8; //设定簇内节点个数最小阈值，参数可更改影响分簇个数和簇内节点个数 (Nmin)
  uint32_t max_adjcluster = 0;
  std::unordered_set<uint32_t> min_nodes;
  std::unordered_set<uint32_t> temp;
  std::unordered_set<uint32_t> cur_temp;
  std::unordered_map<uint32_t,uint32_t> cluster_index; //key--migrate cluster value--join cluster
  for(int i = clusters.size()-1; i >= 0; --i) //后面的簇内节点个数是最少的，所以从后往前遍历
  {
    max_adjcluster = 0;
    min_nodes.clear();
    cur_temp.clear();
    temp.clear();
    if(clusters[i].satellites.size() < min_threshold)
    {
      min_nodes = MinDegree(adj, clusters[i].satellites);// get the lowest degree node in cluster--只有簇内连接度低的节点有邻接簇（环型网络）
      for(auto sat : clusters[i].satellites)
      {
        if(min_nodes.find(sat.id)!=min_nodes.end())
        {
          temp = CanditateCluster(sat, node);
          // /*****打印输出候选簇********/
          // for(auto& t : temp)
          // {
          //   cout<<sat.id+7<<" "<<t<<endl;
          // }
          // /***********************/
          if(temp.size() == 0) continue;
          if(temp.size()>max_adjcluster)
          {
            max_adjcluster = temp.size();
            cur_temp = temp;
          }
        }
      }
      if(cur_temp.size()>0)//添加判断
      {
          uint32_t size = 1000;//INT_MAX
          uint32_t index = 0;
          for(auto& it : cur_temp)
          {
            if(clusters[it].satellites.size() < size)
            {
              size = clusters[it].satellites.size();
              index = it;//Get the cluster including the least nodes
            }
          }
          // cout<<"当前簇："<<i+1<<" 加入簇："<<index+1<<endl;
          cluster_index[i] = index;
          for(auto& t : clusters[i].satellites)
          {
            t.cluster_index = index;
            clusters[index].satellites.push_back(t);
            //更新节点所在的簇索引
            Ptr<Node> node_id = node.Get(t.id);
            node_to_sat[node_id].cluster_index=index;
          }
          clusters[i].satellites.clear();
      } 
    }
  }
}

double 
SatCluster::CalculateFitness(const Cluster& cluster) 
{
  double max_value = 0.0;
  // cluster's all satellite
  for (auto& sat : cluster.satellites) 
  {
    // cout<<"!!"<<sat.id <<" "<<sat.max_utilization <<endl;;
    if (sat.max_utilization > max_value) 
      {
        max_value = sat.max_utilization;
      }
  }
  if(max_value > 0)
    return 1.0 / max_value; // 适应度是最大利用率的倒数
  else
    return numeric_limits<double>::infinity();
}

std::unordered_set<uint32_t>
SatCluster::CanditateCluster(const Satellite& sat, NodeContainer node)
{
  // cout<<node.GetN()<<endl;
  std::unordered_set<uint32_t> clusters;
  uint32_t count = 0;
  vector<vector<int>> adj_list = AdjacenyList(node);// the adj list 
  //如果该节点没有邻接点直接返回空
  if(adj_list[sat.id].size() == 0) return clusters;
  for(uint32_t i = 0; i < adj_list[sat.id].size(); i++)
  {
    uint32_t node_id = adj_list[sat.id][i];
    Ptr<Node> adj_node = node.Get(node_id);
    Satellite sat_node = node_to_sat[adj_node];
    if(sat_node.cluster_index != sat.cluster_index)
    {
      if(count > 3)
      {
        break; // the number of adjacent cluster's max value is 4
      }
      clusters.insert(sat_node.cluster_index);
      // cout<<"cc "<<sat.cluster_index<<" "<<sat_node.cluster_index<<endl;
      count++;
    }
  }
  return clusters;
}

bool
SatCluster::IsMigrate(const Satellite& sat, const vector<Cluster>& clusters, NodeContainer node)
{
  double current_fitness = 0.0;
  double update_fitness = 0.0;
  double threshold = 0.0;
  std::unordered_set<uint32_t> candiate_cluster;
  current_fitness = clusters[sat.cluster_index].fitness; // get the cluster fitness including the satellite node 
  candiate_cluster = CanditateCluster(sat, node); // get the satellite node's candidate cluster (not include the satellite node's cluster)
  // cout<<"C "<<candiate_cluster.size()<<endl;
  // size = 0? --optimize
 
  if(candiate_cluster.size() == 0)
    return false;
  
  // 检查迁移到其他簇的可能性--optimize
  for(auto& it : candiate_cluster)
  {
    Cluster temp_cluster = clusters[it]; //m_cluster
    // Judge whether migrating
    Cluster Sat_cluster = clusters[sat.cluster_index];
    uint32_t temp_num = temp_cluster.satellites.size();
    uint32_t sat_num = Sat_cluster.satellites.size();
    if(sat_num < Nmin || temp_num > Nmax) continue;

    temp_cluster.satellites.push_back(sat);
    update_fitness = max(update_fitness, CalculateFitness(temp_cluster));
    temp_cluster.satellites.pop_back(); // delete the last one
  }
  if(update_fitness != 0.0)
    threshold = (update_fitness - current_fitness) / update_fitness;
  if(update_fitness > current_fitness && (update_fitness - current_fitness) > threshold*0.5)
    return true;
  else
    return false;
}

void 
SatCluster::MigrateSatellite(Satellite& sat, vector<Cluster>& new_clusters, NodeContainer node) 
{
  // 找到最佳适应度的簇
  double bestFitness = 0.0;
  uint32_t bestClusterIndex = sat.cluster_index;
  std::unordered_set<uint32_t> candiate_cluster;
  candiate_cluster = CanditateCluster(sat, node);

  for(auto& it : candiate_cluster)
  {
    Cluster temp_cluster;
    temp_cluster = new_clusters[it];//m_cluster
    // Judge whether migrating
    Cluster sat_cluster = new_clusters[sat.cluster_index];
    uint32_t temp_num = temp_cluster.satellites.size();
    uint32_t sat_num = sat_cluster.satellites.size();
    if(sat_num < Nmin || temp_num > Nmax) continue;

    if (find(temp_cluster.satellites.begin(), temp_cluster.satellites.end(), sat) == temp_cluster.satellites.end()) //????
    {
      temp_cluster.satellites.push_back(sat);
      double fitness = CalculateFitness(temp_cluster);
      if (fitness > bestFitness) 
      {
        bestFitness = fitness;
        bestClusterIndex = it; // index
      }
      temp_cluster.satellites.pop_back();
    }
  }
  
  // 迁移卫星--the num of node in the cluster
  //1.delete 2.update satellite class's cluste index 3.add
  
  if(bestClusterIndex != sat.cluster_index)//?????
  {
    new_clusters[sat.cluster_index].satellites.erase(remove(new_clusters[sat.cluster_index].satellites.begin(), new_clusters[sat.cluster_index].satellites.end(), sat), new_clusters[sat.cluster_index].satellites.end());
    //更新原先簇节点移走后的适应度值
    new_clusters[sat.cluster_index].fitness = CalculateFitness(new_clusters[sat.cluster_index]);
    sat.cluster_index = bestClusterIndex;
    new_clusters[bestClusterIndex].satellites.push_back(sat);
    //更新节点移动到候选簇后的适应度的值
    new_clusters[bestClusterIndex].fitness = bestFitness;
  }
  //!!!update node_to_sat 新添加
  for(auto it : new_clusters)
  {
    for(auto& t : it.satellites)
    {
      Ptr<Node> node_id = node.Get(t.id);
      node_to_sat[node_id] = t;// !!!!!
    }
  }

}

void 
SatCluster::IterateClusters(vector<Cluster>& clusters, int maxIterations, NodeContainer node) 
{
  //vector<Cluster> new_clusters = clusters;

  //cout<<"******* 2.The game cluster phase *******"<<endl;
  bool migrated;
  int iterations = 0;
  do {
      migrated = false;
      for (uint32_t i = 0; i < clusters.size(); i++) 
      {
        // twice judge
        if(clusters[i].satellites.size() == 0)
          continue;
        vector<Satellite> sats = OptionalNode(clusters[i], node);
        if(sats.size() == 0)
          continue;
        for (auto& sat : sats) 
        {   
         // cout<<IsMigrate(sat, clusters, node)<<endl;    
          if(IsMigrate(sat, clusters, node)) 
          {
            cout<<"11"<<endl;
            MigrateSatellite(sat, clusters, node); // update array???
            migrated = true;
          }
        }
      }
      // clusters = move(new_clusters); // 更新簇信息
  } while (migrated && ++iterations < maxIterations);

  // Output cluster result
  // cout << "iteration num: "<<iterations<<endl;
  // uint32_t tt = 0;
  // for(auto it : clusters)
  // {
  //   // cout<<"cluster "<<tt+1<<":"<<" ";
  //   cout<<"簇适应度:"<<it.fitness<<endl;
  //   cout<<"节点最大链路利用率：";
  //   for(auto& t : it.satellites)
  //   {
  //     cout<<t.max_utilization<<" ";
  //   }
  //   cout<<endl;
  //   // tt++;
  // }
}

double
SatCluster::CalculateUtility(const uint32_t& id, const uint32_t& size, const uint32_t& index, const vector<vector<int>>& adj, NodeContainer node)
{
  uint32_t sum = 0;
  double redundancy = 0.0;
  double utility = 0.0;
  uint32_t degree = 0;
  
  degree = adj[index].size();
  Ptr<Node> node_id = node.Get(id);
  // the node of all ports(part ports--only include connecting the node in the same cluster)
  for(uint32_t i = 0; i < node_id->GetNDevices(); i++)
  {
    Ptr<PointToPointNetDevice> p2p = DynamicCast<PointToPointNetDevice>(node_id->GetDevice(i));
    if(p2p != nullptr)
    {
      sum += p2p->GetQueue()->GetNPackets();
    }
  }
  redundancy = sum / size;
  utility = (double)(log(redundancy + 1.0) + log(degree + 1));
  return utility;
}

void
SatCluster::ElectClusterHead(Cluster& cluster, NodeContainer node)
{
  // size = 0/1
  if(cluster.satellites.size() > 1)
  {
    // get the cluster adjacent list--adj_part
    uint32_t n = cluster.satellites.size();
    double sum_utility = 0.0;
    //double max_probability = 0.0;
    std::multimap<double, int> utility_probability; //candiate node id--utility probability
    vector<double> node_utility;
    vector<vector<int>> adj_all = AdjacenyList(node);
    vector<vector<int>> adj_part(n); 
    vector<uint32_t> candiate_head;

    std::unordered_map<uint32_t, uint32_t> id_to_index;// the node id-index (unique)
    // 构建id到index的映射
    for (uint32_t i = 0; i < n; ++i) 
    {
      id_to_index[cluster.satellites[i].id] = i;
    }
    // 构建部分邻接表
    for (uint32_t i = 0; i < n; ++i) 
    {
      uint32_t u = cluster.satellites[i].id;
      for (uint32_t v : adj_all[u]) 
      {
        if (id_to_index.find(v) != id_to_index.end())
        {
          adj_part[i].push_back(id_to_index[v]);
        }
      }
    }

    // Get the cluster betweenness centrality
    vector<double> betweenness_centrality = CalculateBetweennessCentrality(adj_part);
    //double max_value = *max_element(betweenness_centrality.begin(), betweenness_centrality.end());

    //选择最大值和第二大值
    double max_value = 0.0;
    double second_max_value = 0.0; 
    for (double value : betweenness_centrality) 
    {
      if (value > max_value) 
      {
        second_max_value = max_value;
        max_value = value;
      } 
      else if (value > second_max_value && value < max_value) 
      {
        second_max_value = value;
      }
    }

    // Get the candiate cluster head
    for(uint32_t i = 0; i < betweenness_centrality.size(); i++)
    {
      if(betweenness_centrality[i] == max_value || betweenness_centrality[i] == second_max_value)
      {
        for(auto& it : id_to_index)
        {
          if(i == it.second)
          {
            candiate_head.push_back(it.first);
          }
        }
      }
    }

    // Get the candiate node's utility
    for(uint32_t i = 0; i < candiate_head.size(); i++)
    {
      double utility = 0.0;
      utility = CalculateUtility(candiate_head[i], n, id_to_index[candiate_head[i]], adj_part, node);
      node_utility.push_back(utility);
      sum_utility += utility;             
    }
    // Get the candiate node utility probability
    for(uint32_t i = 0; i < candiate_head.size(); i++)
    {
      double value = node_utility[i] / sum_utility;
      utility_probability.insert(pair<double,int>(value,candiate_head[i]));
      //utility_probability[candiate_head[i]] = node_utility[i] / sum_utility; 
    }
    //cout<<"候选集合大小："<<utility_probability.size()<<endl;
    auto it = utility_probability.begin();
    cluster.cluster_head = it->second;
    it++;
    cluster.candidate_head = it->second;
    //cout<<"一级簇首: "<<cluster.cluster_head<<" "<<"二级簇首："<<cluster.candidate_head<<endl;
    
    /*** Get max probabilty--cluster head***/
    // for(auto& it : utility_probability)
    // {
    //   if(it.second > max_probability)
    //   {
    //     cluster.cluster_head = it.first; // upadate cluster head
    //   }
    // }

  }
  if(cluster.satellites.size() == 1)
  {
    cluster.cluster_head = cluster.satellites[0].id;
    //设置候选簇头
    cluster.candidate_head = cluster.satellites[0].id;
  }
}


// Full Connectivity 
void 
SatCluster::dfs(std::map<uint32_t,uint32_t>& visit, const Cluster& cluster, const vector<vector<int>>& adj, uint32_t v)//a node's adjList
{
  uint32_t num = cluster.satellites.size();
    for(uint32_t i = 0; i < num; ++i)
    {
        if( cluster.satellites[i].id != v)
        {
            for(uint32_t j : adj[v]) //change CH's ID
            {
                if( cluster.satellites[i].id == j && visit[cluster.satellites[i].id] == 0)
                {  
                    visit[cluster.satellites[i].id] = 1;
                    dfs(visit, cluster, adj, cluster.satellites[i].id);
                }
            }
        }
    }
}

// judge a cluster connectivity
bool 
SatCluster::IsFullConnectivity(const Cluster& cluster, const vector<vector<int>>& adj)
{
    uint32_t n = cluster.satellites.size();
    std::map<uint32_t,uint32_t> visit;
    //initial the visit
    for(uint32_t i = 0; i != n; i++)
    {
      visit[cluster.satellites[i].id] = 0;
    }
    dfs(visit, cluster, adj, cluster.cluster_head);
    for(uint32_t i = 0; i < n; ++i)
    {
        //cout << "visit[" << cluster.satellites[i].id << "]：" << visit[cluster.satellites[i].id] << "\n";
        if(visit[cluster.satellites[i].id] == 0) return false;//Once finding the not connectivity,return false
    }
    return true;
}

//***********link falut**********//
// if a cluster isn't connectivity, the network return false(start cluster)
// bool 
// SatCluster::IsFullConnectivity(vector<Cluster>& clusters, vector<vector<int>> adj)
// {
//   for(uint32_t m = 0; m < clusters.size(); m++)
//   {
//     uint32_t n = clusters[m].satellites.size();
//     std::map<uint32_t,uint32_t> visit;
//     //initial the visit
//     for(uint32_t i = 0; i != n; i++)
//     {
//       visit[clusters[m].satellites[i].id] = 0;
//     }
//     dfs(visit, clusters[m], adj, clusters[m].cluster_head);
//     for(uint32_t i = 0; i < n; ++i)
//     {
//         if(visit[clusters[m].satellites[i].id] == 0) return false;//Once finding the not connectivity,return false
//     }
//   }
//   return true;
// }

//故障类型收集--更新邻接矩阵
//type--0:节点故障 1:链路故障
void
SatCluster::FaultType(uint32_t id, uint32_t type, vector<Cluster>& clusters, NodeContainer nodes)
{
  //更新邻接矩阵--放到故障外避免每次调用
  vector<vector<int>> adj = AdjacenyList(nodes);

  fault_flag = 0;
  for(auto& t : clusters)
  {
    if(t.cluster_head == id&&type == 1) //判断是否为故障节点是否为簇首节点
    {
      //找到簇首故障所在的簇，判断所在簇的连通性，若不连通则重新分簇
      if(IsFullConnectivity(t, adj) == false)
      {
        m_state.insert(linkClusterHead);
        fault_flag = 1;
      }
    }
    else if(t.cluster_head == id&&type == 0) //如果簇首为节点故障则重新分簇--后续可以通过判断连通性选择候选簇首
    {
      m_state.insert(nodeClusterHead);
      fault_flag = 1;
    }
    else //簇成员节点
    {
      //簇成员节点则需要判断簇内的连通性
      if(IsFullConnectivity(t, adj) == false)
      {
        if(type == 0)
        {
          m_state.insert(nodeClusterMember);
        }
        else
        {
          m_state.insert(linkCLusterMember);
        }
         fault_flag = 1;
      }
    }
  }
}


//根据不同的故障类型进行故障恢复
// void 
// SatCluster::FaultRecovery(NodeContainer nodes)
// {
//   if(fault_flag == 1) //判断链路/节点故障导致簇结构不稳定(不连通)
//   {
//     fault_flag = 0;
//     InitialCluster(nodes); //初始化分簇
//     vector<Cluster> new_clusters = m_cluster;//在初始分簇之后的m_cluster是存在孤立节点的
//     if(m_state.find(nodeClusterHead) != m_state.end() || m_state.find(nodeClusterMember) != m_state.end()) //判断故障类型中是否存在节点故障
//     {
//       m_cluster.clear();
//       for(auto& it : new_clusters)
//       {
//         if(it.satellites.size() != 1)
//         {
//           //更新簇结构--删除孤立节点产生的簇
//           m_cluster.push_back(it);
//         }
//       }
//     }
//     m_state.clear();
//   }
// }

//从控制器故障--簇内不连通（抽象）
vector<NodeContainer>
SatCluster::FaultRecovery(uint32_t node_id, NodeContainer nodes)
{
  // for(auto it : m_cluster)
  // {
  // for(auto&t : it.satellites)
  // {
  //   cout<<t.id + 7<<" ";
  // }
  // cout<<endl;
  // }
  //更新簇头--一级簇头/二级簇头
  vector<NodeContainer>cluster_nodes;
  for(auto& i : m_cluster)
  {
    if(node_id == i.cluster_head + begin_id)
    {
      NodeContainer cluster_node;
      // cout<<"簇id: "<<i.cluster_head<<" 候选簇头: "<<i.candidate_head<<endl;
      Ptr<Node> head_node = nodes.Get(i.candidate_head);
      cluster_node.Add(head_node);
      for(const auto& j : i.satellites)
      {
        if(j.id != i.candidate_head)
        {
          Ptr<Node> member_node = nodes.Get(j.id);
          cluster_node.Add(member_node);
        }
      }
      cluster_nodes.push_back(cluster_node);
    }
    else
    {
      NodeContainer cluster_node;
      Ptr<Node> head_node = nodes.Get(i.cluster_head);
      cluster_node.Add(head_node);
      for(const auto& j : i.satellites)
      {
        if(j.id != i.cluster_head)
        {
          Ptr<Node> member_node = nodes.Get(j.id);
          cluster_node.Add(member_node);
        }
      }
      cluster_nodes.push_back(cluster_node);
    }
  }
  return cluster_nodes;
}

// 初始化分簇接口 -- vector<NodeContainer>
vector<NodeContainer>
SatCluster::InitialTransform(NodeContainer node)
{
  InitialCluster(node);

  // output format: vector<NodeContainer>
  vector<NodeContainer>cluster_nodes;
  for(const auto& i : m_cluster)
  {
    NodeContainer cluster_node;
    Ptr<Node> head_node = node.Get(i.cluster_head);
    cluster_node.Add(head_node);
    for(const auto& j : i.satellites)
    {
      if(j.id != i.cluster_head)
      {
        Ptr<Node> member_node = node.Get(j.id);
        cluster_node.Add(member_node);
      }
    }
    cluster_nodes.push_back(cluster_node);
  }
  return cluster_nodes;
}

//动态分簇接口 -- vector<NodeContainer>
vector<NodeContainer> 
SatCluster::DynamicTransform(int iter_num, NodeContainer nodes, int& dis_flag, int& con_flag)
{
  //博弈迭代
  IterateClusters(m_cluster, iter_num, nodes);
  // cout<<"******* 3.The select cluster head phase *******"<<endl;
  vector<vector<int>> adj_list = AdjacenyList(nodes);// the adj list--每次调用进行更新邻接矩阵
  
  //迭代之后判断网络是否发生链路故障或重连的情况--验证簇的连通性
  //1.链路重连后如果上一时刻簇连通，这个时刻簇仍然依然连通，但是故障节点可能恢复，同步更新邻接表（节点移动更加多样性）
  //2.链路断开之后簇可能不连通，需要判断是否重新分簇--只有链路断开才会产生节点故障
    //判断出现节点的四条链路故障--节点故障
  int node_flag = 0;
  vector<uint32_t> all_node;
  int init_flag = 0;//用于初始化一次
  all_node.clear();
  for(uint32_t i = 0; i < adj_list.size(); i++)
  {
    if(adj_list[i].size() == 0) 
    {
      cout<<"节点故障: "<<nodes.Get(i)->GetId()<<endl;
      node_flag = 1;
    }
    //保存正常节点
    else all_node.push_back(nodes.Get(i)->GetId()); 
  }
//1.链路重连
  if(con_flag == 1)
  {
    con_flag = 0;
    //如果故障节点恢复，需要重新分簇--优化：判断那些故障节点恢复再重新分簇
    std::unordered_set<uint32_t> sat_id;
    for(auto& sat: m_cluster)
    {
      for(auto& t: sat.satellites)
      {
        sat_id.insert(nodes.Get(t.id)->GetId());
      }
    }
    for(uint32_t i = 0; i < all_node.size(); i++)
    {
      if(sat_id.find(all_node[i]) == sat_id.end()) //检查上一时刻节点和当前时刻无故障节点是否一样，不一样则发生新节点故障或故障恢复
      {
          cout<<"链路重连重新分簇"<<endl;
          InitialCluster(nodes);
          init_flag = 1;
          break;
      }
    }
  }
//2.链路故障
  if(dis_flag == 1) //优化的点在于故障之后先初始分簇再动态更新
  {
    dis_flag = 0;
    if(init_flag == 0)//用于判断是否已经初始化分簇了，避免重复
    {
      for(auto& it : m_cluster)
      {
        if(IsFullConnectivity(it,adj_list) == false) //迭代分簇之后由于可见性可能发生簇内不连通
        {
          cout<<"链路断开重新分簇"<<endl;
          InitialCluster(nodes);//初始化分簇，保证簇的连通性
          break;
        }
      }
    }
  }

  if(node_flag == 1)
  {
    node_flag = 0;
    vector<Cluster> new_clusters = m_cluster;//在初始分簇之后的m_cluster是存在孤立节点
    m_cluster.clear();
    for(auto& it : new_clusters)
    {
      if(it.satellites.size() != 1)
      {
        //更新簇结构--删除孤立节点产生的簇--更新节点的簇索引！！！
        m_cluster.push_back(it);
      }
    }
    //更新节点的簇索引
    int index = 0;
    for(auto& it : m_cluster)
    {
      for(auto& t : it.satellites)
      {
        t.cluster_index = index;
        Ptr<Node> node_id = nodes.Get(t.id);
        node_to_sat[node_id] = t;// !!!!!
        //cout<<node_id->GetId()<<"簇索引："<<node_to_sat[node_id].cluster_index<<" ";
      }
      index++;
      //cout<<endl;
    }
  }

  //簇头选举
  int tt = 0;
  for(auto& it : m_cluster)
  {
    tt += 1;
    ElectClusterHead(it, nodes);
    // cout<<"cluster "<<tt<<": ";
    // cout<<"候选簇头 "<<it.candidate_head<<endl;
    // for(auto&t : it.satellites)
    // {
    //   cout<<t.id + 7<<" ";
    // }
    // cout<<endl;
    // cout << "簇的连通性为：" << IsFullConnectivity(it, adj_list) << "\n";
  }
  //输出接口转换
  vector<NodeContainer>cluster_nodes;
  for(const auto& i : m_cluster)
  {
    NodeContainer cluster_node;
    Ptr<Node> head_node = nodes.Get(i.cluster_head);
    cluster_node.Add(head_node);
    for(const auto& j : i.satellites)
    {
      if(j.id != i.cluster_head)
      {
        Ptr<Node> member_node = nodes.Get(j.id);
        cluster_node.Add(member_node);
      }
    }
    cluster_nodes.push_back(cluster_node);
  }
  return cluster_nodes;
}



/**
 * 双层优化主入口
 */

vector<NodeContainer> 
SatCluster::TwoLayerClustering(NodeContainer nodes, const TwoLayerConfig& config)
{
    NS_LOG_FUNCTION(this);
  std::cout << "[TwoLayer] enter, nodes=" << nodes.GetN() << " k_init=" << m_cluster.size() << std::endl;
    
    // 初始化
    m_best_cost = std::numeric_limits<double>::max();
    m_tabu_list.clear();
    uint32_t stall_count = 0;
    uint32_t stall_limit = config.stall_limit ? config.stall_limit : 3;
    
    // 1. 初始化簇数 k_init
    InitialCluster(nodes);
    //uint32_t initial_k = m_cluster.size();

    
    // 2. 外层迭代
    for (uint32_t iter = 0; iter < config.max_outer_iter; ++iter)
    {
        NS_LOG_INFO("======== Outer Iteration " << iter << " (k=" << m_cluster.size() 
                    << ") ========");
      //std::cout << "[TwoLayer] outer_iter=" << iter << " k=" << m_cluster.size() << std::endl;
        
        // 1 内层博弈优化
        IterateClusters(m_cluster, 10, nodes); // 最多10次内层迭代
        
        // 2 选举簇头
        for (auto& cluster : m_cluster)
        {
          ElectClusterHead(cluster, nodes);
        }

        // 3/4 计算代价函数（一次性）
        CostState base = EvaluateCost(nodes, config);
        double F_current = base.cost;
        //std::cout << "[TwoLayer] cost curr=" << F_current << " J_delay=" << base.J_delay
         //   << " J_stable=" << base.J_stable << " connected=" << base.is_connected << std::endl;
        if (!base.is_connected)
        {
          NS_LOG_WARN("Cluster graph is NOT connected!");
        }
        NS_LOG_INFO("Current Cost: F=" << F_current << " (J_delay=" << base.J_delay 
              << ", J_stable=" << base.J_stable << ")");
        
        // 5 更新最优解
        if (F_current < m_best_cost - 1e-6)
        {
            m_best_cost = F_current;
            m_best_k = m_cluster.size();
            m_best_clusters = m_cluster;
            NS_LOG_INFO("*** New Best Solution: k=" << m_best_k 
                        << ", Cost=" << m_best_cost << " ***");
            stall_count = 0;
          //std::cout << "[TwoLayer] best_update k=" << m_best_k << " cost=" << m_best_cost << std::endl;
        }
        else
        {
            stall_count++;
          //std::cout << "[TwoLayer] stall cnt=" << stall_count << " cost=" << F_current << " best=" << m_best_cost << std::endl;
        }

        // 提前停止
        if (stall_count >= stall_limit)
        {
            NS_LOG_INFO("Converged: no improvement for " << stall_limit << " iterations");
            break;
        }
        
        // 6 评估分裂操作
        int split_target = FindSplitTarget(base.metrics, base.adj, config);
        double gain_split = -std::numeric_limits<double>::max();
        bool split_candidate_valid = false;
        ClusterState split_candidate_state;
        
        if (split_target >= 0 && m_cluster.size() < config.k_max)
        {
            // 检查禁忌
            bool in_tabu = false;
            for (const auto& tabu : m_tabu_list)
            {
                if (tabu.first == m_cluster.size() + 1 && tabu.second == "split")
                {
                    in_tabu = true;
                    break;
                }
            }
            
            if (!in_tabu)
            {
                // 备份当前状态
                ClusterState backup = BackupState();
                
                // 尝试分裂
            if (ExecuteSplit(split_target, nodes, base.adj))
                {
                  // 重新运行内层博弈
                  IterateClusters(m_cluster, 5, nodes);
                    
                  // 重新计算代价
                  CostState split_state = EvaluateCost(nodes, config);
                  gain_split = F_current - split_state.cost;
                  NS_LOG_INFO("Split Evaluation: F_split=" << split_state.cost 
                        << ", Gain=" << gain_split);

                  const double improve_eps = 1e-6;
                  if (split_state.is_connected && split_state.cost + improve_eps < F_current)
                  {
                    split_candidate_valid = true;
                    split_candidate_state = BackupState();
                  }

                  // 恢复状态
                  RestoreState(backup);
                }
            }
            else
            {
                NS_LOG_INFO("Split operation in tabu list");
            }
        }
        
        // 7 评估合并操作
        pair<int, int> merge_target = FindMergeTarget(base.metrics, base.adj, config);
        double gain_merge = -std::numeric_limits<double>::max();
        bool merge_candidate_valid = false;
        ClusterState merge_candidate_state;
        
        if (merge_target.first >= 0 && m_cluster.size() > config.k_min)
        {
            // 检查禁忌
            bool in_tabu = false;
            for (const auto& tabu : m_tabu_list)
            {
                if (tabu.first == m_cluster.size() - 1 && tabu.second == "merge")
                {
                    in_tabu = true;
                    break;
                }
            }
            
            if (!in_tabu)
            {
                // 备份当前状态
                ClusterState backup = BackupState();
                
                // 尝试合并
                if (ExecuteMerge(merge_target.first, merge_target.second, nodes))
                {
                    // 重新运行内层博弈
                    IterateClusters(m_cluster, 5, nodes);
                    
                    // 重新计算代价
                    CostState merge_state = EvaluateCost(nodes, config);
                    gain_merge = F_current - merge_state.cost;
                    NS_LOG_INFO("Merge Evaluation: F_merge=" << merge_state.cost 
                          << ", Gain=" << gain_merge);

                    const double improve_eps = 1e-6;
                    if (merge_state.is_connected && merge_state.cost + improve_eps < F_current)
                    {
                      merge_candidate_valid = true;
                      merge_candidate_state = BackupState();
                    }

                    // 恢复状态
                    RestoreState(backup);
                }
            }
            else
            {
                NS_LOG_INFO("Merge operation in tabu list");
            }
        }
        
        // 8 决策：执行最优操作
          // double best_gain = std::max(gain_split, gain_merge);
          // double relative_gain = std::abs(F_current) > 1e-9 ? best_gain / std::abs(F_current) : best_gain;

          // if (best_gain < config.convergence_threshold && relative_gain < config.convergence_threshold)
          // {
          //   ++stall_count;
          // }
          // else
          // {
          //   stall_count = 0;
          // }

          // if (stall_count >= stall_limit)
          // {
          //   NS_LOG_INFO("Converged after stall count. Best k=" << m_best_k << ", Cost=" << m_best_cost);
          //   break;
          // }

          if (split_candidate_valid && (!merge_candidate_valid || gain_split >= gain_merge))
          {
            NS_LOG_INFO("Executing SPLIT operation on cluster " << split_target);
            RestoreState(split_candidate_state);

            // 更新禁忌表
            m_tabu_list.push_back({m_cluster.size(), "split"});
            if (m_tabu_list.size() > config.tabu_size)
            {
              m_tabu_list.pop_front();
            }
          }
          else if (merge_candidate_valid)
          {
            NS_LOG_INFO("Executing MERGE operation: cluster " << merge_target.first 
                  << " + " << merge_target.second);
            RestoreState(merge_candidate_state);
            
            // 更新禁忌表
            m_tabu_list.push_back({m_cluster.size(), "merge"});
            if (m_tabu_list.size() > config.tabu_size)
            {
              m_tabu_list.pop_front();
            }
          }
          else
          {
            NS_LOG_INFO("No beneficial split/merge found. Stopping.");
            break;
          }
    }
    
    // 9 应用最优解
    if (!m_best_clusters.empty())
    {
        m_cluster = m_best_clusters;
        NS_LOG_INFO("Applied best solution: k=" << m_best_k << ", Cost=" << m_best_cost);
    }
    
    // 10 最终簇头选举
    CostState final_state = EvaluateCost(nodes, config);
    if (!final_state.is_connected)
    {
      NS_LOG_WARN("Best solution graph is NOT connected during finalization");
    }
    vector<vector<int>> final_adj = final_state.adj;
    for (auto& cluster : m_cluster)
    {
        ElectClusterHead(cluster, nodes);
    }
    
    // 11 输出格式转换
    vector<NodeContainer>cluster_nodes;
    for(const auto& i : m_cluster)
    {
      NodeContainer cluster_node;
      Ptr<Node> head_node = nodes.Get(i.cluster_head);
      cluster_node.Add(head_node);
      for(const auto& j : i.satellites)
      {
        if(j.id != i.cluster_head)
        {
          Ptr<Node> member_node = nodes.Get(j.id);
          cluster_node.Add(member_node);
        }
      }
      cluster_nodes.push_back(cluster_node);
    }
    return cluster_nodes;
}



/**
 * 修正版时延代价计算
 * 基于实际簇间路径和真实跳数
 */
double 
SatCluster::ComputeDelayTarget(const vector<ClusterMetrics>& metrics, 
                                NodeContainer nodes,
                                const vector<vector<int>>& adj)
{
    if (metrics.empty()) return std::numeric_limits<double>::max();
    
    uint32_t N = nodes.GetN();
    uint32_t k = metrics.size();
    
    // ========== 第一项：簇内平均跳数 ==========
    double J_intra = 0.0;
    for (size_t i = 0; i < metrics.size(); ++i)
    {
        uint32_t cluster_size = m_cluster[i].satellites.size();
        // 权重：该簇的通信对占总通信对的比例
        double weight = static_cast<double>(cluster_size * (cluster_size - 1)) / 
                       (N * (N - 1) + 1e-9);
        J_intra += metrics[i].avg_intra_hops * weight;
    }
    
    // ========== 第二项：簇间路由延迟 ==========
    // 构建簇间图
    vector<vector<uint32_t>> cluster_graph = BuildClusterGraph(adj);
    
    // 检查连通性（硬约束）
    if (!IsClusterGraphConnected(cluster_graph))
    {
        NS_LOG_WARN("Cluster graph is NOT connected!");
        return std::numeric_limits<double>::max();
    }
    
    // 计算簇间距离矩阵
    vector<vector<uint32_t>> cluster_dist = ComputeClusterDistances(cluster_graph);
    
    // 计算加权平均跨簇路径长度
    double avg_h_cross = 0.0;
    uint32_t cross_count = 0;
    
    for (uint32_t i = 0; i < k; ++i)
    {
        for (uint32_t j = i + 1; j < k; ++j)
        {
            uint32_t size_i = m_cluster[i].satellites.size();
            uint32_t size_j = m_cluster[j].satellites.size();
            
            // 簇i和簇j之间的通信对数量
            uint32_t pairs = size_i * size_j;
            
            // 加权累加跨簇跳数
            avg_h_cross += cluster_dist[i][j] * pairs;
            cross_count += pairs;
        }
    }
    
    if (cross_count > 0)
    {
        avg_h_cross /= cross_count;
    }
    
    // 跨簇通信概率
    double P_cross = (k > 1) ? static_cast<double>(cross_count) / (N * (N - 1) + 1e-9) : 0.0;
    
    // 簇间延迟 = (簇内到网关 + 跨簇跳数 + 网关到目标)
    double J_inter = (J_intra / 2.0 + avg_h_cross) * P_cross;
    
    // ========== 第三项：拥塞惩罚 ==========
    double L_max = 0.0;
    for (const auto& metric : metrics)
    {
        L_max = std::max(L_max, metric.max_boundary_util);
    }
    
    // M/M/1 排队延迟模型
    double J_queue = 0.0;
    if (L_max < 0.99)
    {
        J_queue = L_max / (1.0 - L_max);
    }
    else
    {
        J_queue = 100.0; // 近乎饱和，高惩罚
    }
    
    // ========== 总时延代价 ==========
    double w1 = 1.0;  // 簇内权重
    double w2 = 2.0;  // 簇间权重（更重要）
    double w3 = 0.5;  // 拥塞惩罚权重
    
    double J_delay = w1 * J_intra + w2 * J_inter + w3 * J_queue;
    
    NS_LOG_INFO("Delay breakdown: J_intra=" << J_intra 
                << ", J_inter=" << J_inter 
                << ", J_queue=" << J_queue 
                << ", avg_h_cross=" << avg_h_cross);
    
    return J_delay;
}

/**
 * 修正版稳定性代价计算
 * 仅基于min-cut和簇规模，不考虑链路寿命
 */
double 
SatCluster::ComputeStabilityTarget(const vector<ClusterMetrics>& metrics, 
                                    const vector<vector<int>>& adj)
{
    uint32_t k = m_cluster.size();
    double J_stable = 0.0;
    
    // 构建簇间图
    vector<vector<uint32_t>> cluster_graph = BuildClusterGraph(adj);
    
    for (size_t i = 0; i < k; ++i)
    {
        // 计算簇i的最小割（与外界的连接强度）
        uint32_t min_cut = GetMinCut(m_cluster[i], adj);
        
        // 如果min_cut=0，说明该簇孤立，极高惩罚
        if (min_cut == 0)
        {
            NS_LOG_WARN("Cluster " << i << " is ISOLATED (min_cut=0)!");
            return std::numeric_limits<double>::max();
        }
        
        // 簇规模权重（大簇断连影响更严重）
        double cluster_weight = static_cast<double>(m_cluster[i].satellites.size()) / 
                               (m_satellite.size() + 1e-9);
        
        // 连通脆弱度 V(C_i) = 1 / min_cut
        double vulnerability = 1.0 / min_cut;
        
        // 加权累加
        J_stable += cluster_weight * vulnerability;
    }
    
    // **关键**：增加k的惩罚项（k越大，簇越多，风险越高）
    // 理由：k大时，每个簇的跨轨链路更少，更容易孤立
    double k_penalty = 0.01 * k;  // 线性惩罚
    
    J_stable += k_penalty;
    
    // 簇规模不平衡惩罚
    double size_std = 0.0;
    double avg_size = static_cast<double>(m_satellite.size()) / k;
    for (const auto& cluster : m_cluster)
    {
        double diff = cluster.satellites.size() - avg_size;
        size_std += diff * diff;
    }
    size_std = std::sqrt(size_std / k);
    double balance_penalty = 0.005 * size_std;
    J_stable += balance_penalty;
    
    NS_LOG_INFO("Stability: base=" << (J_stable - k_penalty - balance_penalty)
                << ", k_penalty=" << k_penalty << ", balance_penalty=" << balance_penalty);

    return J_stable;
}

/**
 * 构建簇间图（加权）
 * 返回邻接矩阵 cluster_graph[i][j] = 边界链路数量（0表示不相邻）
 */
vector<vector<uint32_t>> 
SatCluster::BuildClusterGraph(const vector<vector<int>>& adj)
{
    uint32_t k = m_cluster.size();
    vector<vector<uint32_t>> cluster_graph(k, vector<uint32_t>(k, 0));
    
    // 为每个节点建立 节点ID → 簇ID 的映射
    std::unordered_map<uint32_t, uint32_t> node_to_cluster;
    for (size_t i = 0; i < m_cluster.size(); ++i)
    {
        for (const auto& sat : m_cluster[i].satellites)
        {
            node_to_cluster[sat.id] = i;
        }
    }
    
    // 遍历所有边，统计跨簇链路
    for (size_t u = 0; u < adj.size(); ++u)
    {
        if (node_to_cluster.find(u) == node_to_cluster.end()) continue;
        uint32_t cluster_u = node_to_cluster[u];
        
        for (int v : adj[u])
        {
            if (node_to_cluster.find(v) == node_to_cluster.end()) continue;
            uint32_t cluster_v = node_to_cluster[v];
            
            if (cluster_u != cluster_v)
            {
                cluster_graph[cluster_u][cluster_v]++;
            }
        }
    }
    
    return cluster_graph;
}

/**
 * 计算簇间最短路径矩阵（基于BFS）
 * 返回 dist[i][j] = 簇i到簇j的跳数（INF表示不可达）
 */
vector<vector<uint32_t>> 
SatCluster::ComputeClusterDistances(const vector<vector<uint32_t>>& cluster_graph)
{
    uint32_t k = cluster_graph.size();
    const uint32_t INF = 1000000;
    vector<vector<uint32_t>> dist(k, vector<uint32_t>(k, INF));
    
    // 初始化直接相邻的簇
    for (uint32_t i = 0; i < k; ++i)
    {
        dist[i][i] = 0;
        for (uint32_t j = 0; j < k; ++j)
        {
            if (i != j && cluster_graph[i][j] > 0)
            {
                dist[i][j] = 1;
            }
        }
    }
    
    // Floyd-Warshall 算法
    for (uint32_t via = 0; via < k; ++via)
    {
        for (uint32_t i = 0; i < k; ++i)
        {
            for (uint32_t j = 0; j < k; ++j)
            {
                if (dist[i][via] != INF && dist[via][j] != INF)
                {
                    dist[i][j] = std::min(dist[i][j], dist[i][via] + dist[via][j]);
                }
            }
        }
    }
    
    return dist;
  }

  /**
 * 检查簇间图连通性
 */
bool 
SatCluster::IsClusterGraphConnected(const vector<vector<uint32_t>>& cluster_graph)
{
    if (cluster_graph.empty()) return true;
    
    uint32_t k = cluster_graph.size();
    vector<bool> visited(k, false);
    std::queue<uint32_t> q;
    
    // BFS从簇0开始
    q.push(0);
    visited[0] = true;
    uint32_t visited_count = 1;
    
    while (!q.empty())
    {
        uint32_t u = q.front();
        q.pop();
        
        for (uint32_t v = 0; v < k; ++v)
        {
            if (!visited[v] && cluster_graph[u][v] > 0)
            {
                visited[v] = true;
                visited_count++;
                q.push(v);
            }
        }
    }
    
    return visited_count == k;
}
/**
 * 检查簇间连通性（对外接口）
 */
bool 
SatCluster::CheckInterClusterConnectivity(const vector<vector<int>>& adj)
{
    vector<vector<uint32_t>> cluster_graph = BuildClusterGraph(adj);
    return IsClusterGraphConnected(cluster_graph);
}


// ========== 分裂操作 ==========

int 
SatCluster::FindSplitTarget(const vector<ClusterMetrics>& metrics, 
                            const vector<vector<int>>& adj,
                            const TwoLayerConfig& config)
{
    int best_target = -1;
    double best_score = -std::numeric_limits<double>::max();
    
    for (size_t i = 0; i < m_cluster.size(); ++i)
    {
        const Cluster& cluster = m_cluster[i];
        uint32_t size = cluster.satellites.size();
        
        // 太小不能分裂
        if (size < 4) continue;
        
        // 评分：优先分裂大簇、高直径、高负载的簇
        double score = 0.0;
        
        // 规模因子
        score += std::log(size + 1);
        
        // 直径因子
        if (metrics[i].diameter > config.D_max)
        {
            score += 2.0 * (metrics[i].diameter - config.D_max);
        }
        
        // 负载因子
        score += metrics[i].max_boundary_util;
        
        // 连通性因子（min-cut太小容易断连，分裂后更稳定）
        uint32_t min_cut = GetMinCut(cluster, adj);
        if (min_cut < 3)
        {
            score += 1.0;
        }
        
        if (score > best_score)
        {
            best_score = score;
            best_target = i;
        }
    }
    
    NS_LOG_INFO("Split target: cluster " << best_target << " with score " << best_score);
    return best_target;
}

bool 
SatCluster::ExecuteSplit(uint32_t cluster_idx, NodeContainer nodes, 
                        const vector<vector<int>>& adj)
{
  if (cluster_idx >= m_cluster.size()) return false;
    
  Cluster& target = m_cluster[cluster_idx];
  if (target.satellites.size() < 4) return false;

  auto isSubConnected = [&](const vector<Satellite>& group) -> bool
  {
    if (group.empty()) return false;
    std::unordered_set<uint32_t> ids;
    for (const auto& s : group) ids.insert(s.id);
    std::queue<uint32_t> q;
    std::unordered_set<uint32_t> visited;
    q.push(group.front().id);
    visited.insert(group.front().id);
    while (!q.empty())
    {
      uint32_t u = q.front(); q.pop();
      for (uint32_t v : adj[u])
      {
        if (ids.count(v) && !visited.count(v))
        {
          visited.insert(v);
          q.push(v);
        }
      }
    }
    return visited.size() == ids.size();
  };
    
    // 使用谱聚类方法分裂
    // 1. 构建簇内拉普拉斯矩阵
    uint32_t n = target.satellites.size();
    std::unordered_map<uint32_t, uint32_t> id_to_idx;
    for (uint32_t i = 0; i < n; ++i)
    {
        id_to_idx[target.satellites[i].id] = i;
    }
    
    // 2. 找到两个最远的节点作为初始中心
    vector<vector<uint32_t>> dist = ComputeAllPairsDistance(target, adj);
    uint32_t max_dist = 0;
    uint32_t center1_idx = 0, center2_idx = 1;
    for (uint32_t i = 0; i < n; ++i)
    {
        for (uint32_t j = i + 1; j < n; ++j)
        {
            if (dist[i][j] > max_dist)
            {
                max_dist = dist[i][j];
                center1_idx = i;
                center2_idx = j;
            }
        }
    }
    
    // 3. 基于距离分配节点
    vector<Satellite> group1, group2;
    for (uint32_t i = 0; i < n; ++i)
    {
        if (dist[i][center1_idx] < dist[i][center2_idx])
        {
            group1.push_back(target.satellites[i]);
        }
        else
        {
            group2.push_back(target.satellites[i]);
        }
    }
  
    // 4. 检查分裂有效性?连通性
    if (group1.empty() || group2.empty()) return false;
    if (!isSubConnected(group1) || !isSubConnected(group2))
    {
      NS_LOG_WARN("Split produces disconnected sub-clusters, rejected");
      return false;
    }
    
    // 5. 创建新簇并更新
    Cluster new_cluster1, new_cluster2;
    new_cluster1.satellites = group1;
    new_cluster2.satellites = group2;
    new_cluster1.fitness = 0.0;
    new_cluster2.fitness = 0.0;
    
    // 更新索引
    uint32_t new_idx = m_cluster.size();
    auto backup_clusters = m_cluster;
    auto backup_mapping = node_to_sat;
    for (auto& sat : new_cluster2.satellites)
    {
      sat.cluster_index = new_idx;
      Ptr<Node> node_id = nodes.Get(sat.id);
      node_to_sat[node_id].cluster_index = new_idx;
    }

    // 替换原簇并临时构造新结构用于连通性检查
    m_cluster[cluster_idx] = new_cluster1;
    m_cluster.push_back(new_cluster2);

    vector<vector<uint32_t>> cluster_graph = BuildClusterGraph(adj);
    if (!IsClusterGraphConnected(cluster_graph))
    {
      // 恢复
      m_cluster = backup_clusters;
      node_to_sat = backup_mapping;
      NS_LOG_WARN("Split breaks inter-cluster connectivity, rejected");
      return false;
    }

    NS_LOG_INFO("Split cluster " << cluster_idx << " into sizes: " 
          << group1.size() << " and " << group2.size());
    
    return true;
}

// ========== 合并操作 ==========

pair<int, int> 
SatCluster::FindMergeTarget(const vector<ClusterMetrics>& metrics,
                            const vector<vector<int>>& adj,
                            const TwoLayerConfig& config)
{
    pair<int, int> best_pair = {-1, -1};
    double best_score = std::numeric_limits<double>::max();
    
    vector<vector<uint32_t>> cluster_graph = BuildClusterGraph(adj);
    
    for (size_t i = 0; i < m_cluster.size(); ++i)
    {
        for (size_t j = i + 1; j < m_cluster.size(); ++j)
        {
            // 必须相邻
            if (cluster_graph[i][j] == 0) continue;
            
            uint32_t size_i = m_cluster[i].satellites.size();
            uint32_t size_j = m_cluster[j].satellites.size();
            uint32_t merged_size = size_i + size_j;
            
            // 合并后太大则跳过
            if (merged_size > Nmax) continue;

            // 合并后跨轨链路不足则跳过
            if (config.M_min > 0 && cluster_graph[i][j] < config.M_min) continue;
            
            // 评分：优先合并小簇、相邻紧密的簇
            double score = 0.0;
            
            // 规模惩罚（合并后越大越不好）
            score += std::log(merged_size + 1);
            
            // 链接强度奖励（边界链路越多越好）
            score -= std::log(cluster_graph[i][j] + 1);
            
            // 规模不平衡惩罚
            double size_diff = std::abs(static_cast<int>(size_i) - static_cast<int>(size_j));
            score += 0.1 * size_diff;
            
            if (score < best_score)
            {
                best_score = score;
                best_pair = {i, j};
            }
        }
    }
    
    NS_LOG_INFO("Merge target: clusters " << best_pair.first << " and " 
                << best_pair.second << " with score " << best_score);
    
    return best_pair;
}

bool 
SatCluster::ExecuteMerge(uint32_t cluster_i, uint32_t cluster_j, NodeContainer nodes)
{
    if (cluster_i >= m_cluster.size() || cluster_j >= m_cluster.size()) return false;
    if (cluster_i == cluster_j) return false;

  auto backup_clusters = m_cluster;
  auto backup_mapping = node_to_sat;
    
    // 合并到i
    for (auto& sat : m_cluster[cluster_j].satellites)
    {
        sat.cluster_index = cluster_i;
        m_cluster[cluster_i].satellites.push_back(sat);
        
        Ptr<Node> node_id = nodes.Get(sat.id);
      node_to_sat[node_id].cluster_index = cluster_i;
    }
    
    // 删除簇j
    m_cluster.erase(m_cluster.begin() + cluster_j);
    
    // 更新所有后续簇的索引
    for (size_t k = cluster_j; k < m_cluster.size(); ++k)
    {
        for (auto& sat : m_cluster[k].satellites)
        {
            sat.cluster_index = k;
            Ptr<Node> node_id = nodes.Get(sat.id);
            node_to_sat[node_id].cluster_index = k;
        }
    }

        vector<vector<uint32_t>> cluster_graph = BuildClusterGraph(AdjacenyList(nodes));
        if (!IsClusterGraphConnected(cluster_graph))
        {
          m_cluster = backup_clusters;
          node_to_sat = backup_mapping;
          NS_LOG_WARN("Merge breaks inter-cluster connectivity, rejected");
          return false;
        }
    
    NS_LOG_INFO("Merged clusters " << cluster_i << " and " << cluster_j 
                << ", new size: " << m_cluster[cluster_i].satellites.size());
    
    return true;
}



// ========== 簇性能指标计算 ==========

vector<ClusterMetrics> 
SatCluster::ComputeClusterMetrics(NodeContainer nodes, const vector<vector<int>>& adj)
{
    vector<ClusterMetrics> metrics;
    
    for (const auto& cluster : m_cluster)
    {
        ClusterMetrics metric;
        
        metric.avg_intra_hops = ComputeAvgIntraHops(cluster, adj);
        metric.diameter = ComputeClusterDiameter(cluster, adj);
        metric.inter_links_count = CountInterOrbitLinks(cluster, adj);
        metric.min_link_lifetime = 100.0;
        
        metric.max_boundary_util = 0.0;
        for (const auto& sat : cluster.satellites)
        {
            metric.max_boundary_util = std::max(metric.max_boundary_util, 
                                                sat.max_utilization);
        }
        
        metrics.push_back(metric);
    }
    
    return metrics;
}


uint32_t 
SatCluster::ComputeClusterDiameter(const Cluster& cluster, const vector<vector<int>>& adj)
{
    if (cluster.satellites.size() <= 1) return 0;
    
    vector<vector<uint32_t>> dist = ComputeAllPairsDistance(cluster, adj);
    
    uint32_t diameter = 0;
    for (const auto& row : dist)
    {
        for (uint32_t d : row)
        {
            if (d < 1000000)
            {
                diameter = std::max(diameter, d);
            }
        }
    }
    
    return diameter;
}

double 
SatCluster::ComputeAvgIntraHops(const Cluster& cluster, const vector<vector<int>>& adj)
{
    if (cluster.satellites.size() <= 1) return 0.0;
    
    vector<vector<uint32_t>> dist = ComputeAllPairsDistance(cluster, adj);
    
    double total_hops = 0.0;
    uint32_t count = 0;
    uint32_t n = cluster.satellites.size();
    
    for (uint32_t i = 0; i < n; ++i)
    {
        for (uint32_t j = i + 1; j < n; ++j)
        {
            if (dist[i][j] < 1000000)
            {
                total_hops += dist[i][j];
                count++;
            }
        }
    }
    
    return (count > 0) ? (total_hops / count) : 0.0;
}

vector<vector<uint32_t>> 
SatCluster::ComputeAllPairsDistance(const Cluster& cluster, 
                                    const vector<vector<int>>& adj)
{
    uint32_t n = cluster.satellites.size();
    const uint32_t INF = 1000000;
    vector<vector<uint32_t>> dist(n, vector<uint32_t>(n, INF));
    
    // 建立节点ID到索引的映射
    std::unordered_map<uint32_t, uint32_t> id_to_idx;
    for (uint32_t i = 0; i < n; ++i)
    {
        id_to_idx[cluster.satellites[i].id] = i;
        dist[i][i] = 0;
    }
    
    // 初始化直接相邻的边
    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t node_id = cluster.satellites[i].id;
        for (int neighbor : adj[node_id])
        {
            if (id_to_idx.find(neighbor) != id_to_idx.end())
            {
                uint32_t j = id_to_idx[neighbor];
                dist[i][j] = 1;
            }
        }
    }
    
    // Floyd-Warshall
    for (uint32_t k = 0; k < n; ++k)
    {
        for (uint32_t i = 0; i < n; ++i)
        {
            for (uint32_t j = 0; j < n; ++j)
            {
                if (dist[i][k] != INF && dist[k][j] != INF)
                {
                    dist[i][j] = std::min(dist[i][j], dist[i][k] + dist[k][j]);
                }
            }
        }
    }
    
    return dist;
}

uint32_t 
SatCluster::CountInterOrbitLinks(const Cluster& cluster, const vector<vector<int>>& adj)
{
    // 简化：统计跨簇边界链路数量
    uint32_t count = 0;
    std::unordered_set<uint32_t> cluster_nodes;
    
    for (const auto& sat : cluster.satellites)
    {
        cluster_nodes.insert(sat.id);
    }
    
    for (const auto& sat : cluster.satellites)
    {
        for (int neighbor : adj[sat.id])
        {
            if (cluster_nodes.find(neighbor) == cluster_nodes.end())
            {
                count++;
            }
        }
    }
    
    return count;
}

uint32_t 
SatCluster::GetMinCut(const Cluster& cluster, const vector<vector<int>>& adj)
{
    // 计算最小割：簇与外界的边界链路数量
    uint32_t min_cut = 0;
    std::unordered_set<uint32_t> cluster_nodes;
    
    for (const auto& sat : cluster.satellites)
    {
        cluster_nodes.insert(sat.id);
    }
    
    for (const auto& sat : cluster.satellites)
    {
        for (int neighbor : adj[sat.id])
        {
            if (cluster_nodes.find(neighbor) == cluster_nodes.end())
            {
                min_cut++;
            }
        }
    }
    
    return min_cut;
}