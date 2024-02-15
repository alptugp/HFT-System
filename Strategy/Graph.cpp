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
        adjList.resize(V, std::vector<double>(V, 0.0));
    }

    void addEdge(int u, int v, double weight) {
        adjList[u][v] = weight;
    }

    std::pair<double, double> findNegativeCycle() {
        int startVertex = 0;
        std::vector<double> cycleSums;
        
        for (int direction = 1; direction < V; direction++) {
            int currentNode = startVertex;
            int nextNode = direction; 
            double weightSum = 0;
            do {
                weightSum += adjList[currentNode][nextNode];
                for (int node = 0; node < V; node++) {
                    if ((node != currentNode) && (node != nextNode)) {  
                        currentNode = nextNode;
                        nextNode = node;
                        break;
                    }
                }
            } while (currentNode != startVertex);
            cycleSums.emplace_back(weightSum);
        }

        double weightSumFirstDirection = cycleSums[0];
        double weightSumSecondDirection = cycleSums[1];
        return std::make_pair(weightSumFirstDirection, weightSumSecondDirection);
    }
};

int main() {
    Graph graph(3);
    graph.addEdge(0, 1, 0.1);
    graph.addEdge(1, 0, -0.1);

    graph.addEdge(1, 2, -0.2);
    graph.addEdge(2, 1, 0.0);

    graph.addEdge(2, 0, -0.3);
    graph.addEdge(0, 2, 0.0);

    std::pair<double, double> cycleSums = graph.findNegativeCycle();
    std::cout << "First Direction Sum: " << cycleSums.first << std::endl;
    std::cout << "Second Direction Sum: " << cycleSums.second << std::endl;
    return 0;
}
