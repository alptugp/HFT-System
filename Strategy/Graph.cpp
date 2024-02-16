#include <iostream>
#include <vector>
#include <limits>
#include <unordered_map>

class Graph {
private:
    int V;
    std::vector<std::vector<double>> adjList;

public:
    Graph(int vertices) : V(vertices) {
        adjList.resize(V, std::vector<double>(V, 1.0));
    }

    void addEdge(int u, int v, double weight) {
        adjList[u][v] = weight;
    }

    std::pair<double, double> findTriangularArbitrage() {
        int startVertex = 0;
        std::vector<double> cycleWeightMultiplcations;
        
        for (int direction = 1; direction < V; direction++) {
            int currentNode = startVertex;
            int nextNode = direction; 
            double weightMultiply = 1;
            do {
                weightMultiply *= adjList[currentNode][nextNode];
                for (int node = 0; node < V; node++) {
                    if ((node != currentNode) && (node != nextNode)) {  
                        currentNode = nextNode;
                        nextNode = node;
                        break;
                    }
                }
            } while (currentNode != startVertex);
            cycleWeightMultiplcations.emplace_back(weightMultiply);
        }

        double weightMultiplyFirstDirection = cycleWeightMultiplcations[0];
        double weightMultiplySecondDirection = cycleWeightMultiplcations[1];
        return std::make_pair(weightMultiplyFirstDirection, weightMultiplySecondDirection);
    }
};
