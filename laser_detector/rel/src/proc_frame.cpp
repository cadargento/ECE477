#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/types.hpp>
#include <list>
#include <iostream>
#include <thread>

#include <sys/socket.h> 
#include <sys/ioctl.h>
#include <netdb.h> 
#include <sys/types.h> 
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <unistd.h>

#define THRESH 50
#define THREADS 4
#define N 4
#define MESSAGE_SIZE 1000000

#define EXIT 0x95
#define PROC_FRAME 0x96
#define XY_DATA 0x97
#define BASE_FRAME 0x98

#define READ_ERR -1
#define READ_SUCCESS 0

using namespace cv;
using namespace std;

uchar* get_pixel(Mat img, int x, int y);

void detect_in_frame_worker(Mat img, Mat base, std::list<cv::Point3d> & largest_vals_ptr, int min_col, int max_col);
Point detect_in_frame_threads(Mat img, Mat base);
int read_message(int socketfd);
int read_img_socket(int socketfd, Mat & dest);

FILE* log_fd;
int main(int argc, char** argv)
{
	log_fd = fopen("ProcessFrame.log", "w");
	if (argc != 2)
	{
		fprintf(log_fd, "Propper Usage: ProcessFrame <port>\n");
		return 1;
	}

	int port = atoi(argv[1]);

	//Create socket
	int socketfd;
	struct sockaddr_in servAddr;

	if((socketfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		fprintf(log_fd, "Could not create socket\n");
		return 1;
	} else {
		fprintf(log_fd, "Socket created\n");
	}

	//Make connection
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(port);
	servAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	bzero(&(servAddr.sin_zero), 8);

	fprintf(log_fd, "Connecting... ");
	if (connect(socketfd, (struct sockaddr *)&servAddr, sizeof(servAddr))  == -1) {
		fprintf(log_fd, "\nConnect Failed\n");
	}
	fprintf(log_fd, "Done!\n");

	Mat base_frame;
	Mat frame;
	uint8_t header;
	char send_msg[16];
	Point pos;

	fd_set referenced_fds;


	while (true)
	{
		//Wait for request (select blocks until resource is ready)
		FD_ZERO(&referenced_fds);
		FD_SET(socketfd,&referenced_fds);
		select(socketfd + 1, &referenced_fds, NULL, NULL, NULL);

		if(FD_ISSET(socketfd, &referenced_fds)){ //Process Request

			header = read_message(socketfd);
			
			if (header == EXIT)
			{
				fprintf(log_fd, "Received code exit, exiting gracefully\n");
				break;
			} else if (header == PROC_FRAME) {
				
				read_img_socket(socketfd, std::ref(frame));

				//Process frame
				pos = detect_in_frame_threads(frame, base_frame);

				// Send data back
				send_msg[0] = XY_DATA;
				//Put 4 bytes for x and y
				for (int i = 0; i < 4; i++)
				{
					send_msg[1 + i] = ((char *) &(pos.x))[i];
					send_msg[5 + i] = ((char *) &(pos.y))[i];
				}
				send(socketfd, send_msg, 8, 0);

			} else if (header == BASE_FRAME) {
				//Get frame from socket
				read_img_socket(socketfd, std::ref(base_frame));

			} else if (header == READ_ERR) {
				fprintf(log_fd, "Read error when interpreting message header, exiting gracefully... \n");
				break;
			} 
			else {
				fprintf(log_fd, "Unrecognized code\n");
			}
		}
	}

	fclose(log_fd);

	return 0;
}


int read_message(int socketfd){
	uint8_t header;
	if(read(socketfd, &(header), 1) < 0){
		return READ_ERR;
	}

	return header;
}

int read_img_socket(int socketfd, Mat & dest)
{
	uint32_t img_size;

	if(read(socketfd, &(img_size), 4) < 0){
		return READ_ERR;
	}

	uint32_t read_progress = 0;
	uint32_t buff_size;

	std::vector<char> img_data;

	while (read_progress < img_size)
	{
		if(ioctl(socketfd, FIONREAD, &buff_size) == -1){
			fprintf(log_fd, "Error reading message size\n");
			return READ_ERR;
		}
		if(read(socketfd, &(img_data[read_progress]), buff_size) < 0){
			fprintf(log_fd, "Error reading message data\n");
			return READ_ERR;
		}
		read_progress += buff_size;
	}

	Mat img = imdecode(Mat(img_data), 1);
	return READ_SUCCESS;
}


void detect_in_frame_worker(Mat img, Mat base, std::list<cv::Point3d> & largest_vals, int min_col, int max_col){
	uint16_t height = img.size().height;
	uint16_t width = img.size().width;

	largest_vals.push_front(cv::Point3d(0, 0, THRESH));
	
	for (int row_idx = 0; row_idx < height; row_idx++)
	{
		for (int col_idx = min_col; col_idx < max_col; col_idx++)
		{
			// Calculate diff
			int val = get_pixel(img, col_idx, row_idx)[1] - get_pixel(base, col_idx, row_idx)[1];
			// Add to list
			if (val > largest_vals.back().z)
			{
				for (std::list<Point3d>::iterator it = largest_vals.begin(); it != largest_vals.end(); it++){
					if (val > (*it).z)
					{
						largest_vals.insert(it, Point3d(col_idx, row_idx, val));
						if (largest_vals.size() > N)
						{
							largest_vals.pop_back();
						}
						break;
					}
				}
			}

		}
	}

	if (largest_vals.back().z == THRESH){
		largest_vals.pop_back();
	}

}

Point detect_in_frame_threads(Mat img, Mat base){
	std::thread workers[THREADS];
	std::list<cv::Point3d> queues[THREADS];	

	Point3d maxes[N];

	int min_col;
	const int thickness = img.size().width / THREADS;
	int max_col;

	min_col = 0;
	max_col = min_col + thickness;

	for (int i = 0; i < THREADS; ++i)
	{

		workers[i] = std::thread(detect_in_frame_worker, img, base, std::ref(queues[i]), min_col, max_col);

		min_col = max_col + 1;

		if (i == THREADS - 2)
		{
			max_col = img.size().width;
		} else
		{
			max_col = min_col + thickness;
		}
	}

	int strips_with_multiple_count = 0;
	int strip_with_multiple;
	for (int i = 0; i < THREADS; i++)
	{
		workers[i].join();
		if (queues[i].size() > 0)
		{
			strips_with_multiple_count++;
			strip_with_multiple = i;
		}
	}

	if (strips_with_multiple_count == 0)
	{ //Not found 
		return Point(0, 0);
	} else if (strips_with_multiple_count == 1)
	{
		Point3d sum;
	
		for (std::list<Point3d>::iterator it = queues[strip_with_multiple].begin(); it != queues[strip_with_multiple].end(); it++){
			sum += *it;
		}

		int row_avg = sum.y / N;
		int col_avg = sum.x / N;

		return Point(col_avg, row_avg);
	} else {
		printf("CONSOLIDATION FUNCTIONALITY NOT WORKING YET\n");

		return Point(0, 0);
	}

	
}

uchar* get_pixel(Mat img, int x, int y){
	return (& img.at<uchar>(y, x, 0));
}