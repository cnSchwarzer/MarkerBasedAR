#include<opencv2\opencv.hpp>
#include<iostream>
#include<math.h>

using namespace cv;
using namespace std;

class MarkerBasedARProcessor
{
	Mat Image, ImageGray, ImageAdaptiveBinary;
	vector<vector<Point>> ImageContours;
	vector<vector<Point2f>> ImageQuads, ImageMarkers;
	vector<Point2f> FlatMarkerCorners;
	Size FlatMarkerSize;

	uchar CorrectMarker[7 * 7] =
	{
		0,0,0,0,0,0,0,
		0,0,0,0,0,255,0,
		0,0,255,255,255,0,0,
		0,255,255,255,0,255,0,
		0,255,255,255,0,255,0,
		0,255,255,255,0,255,0,
		0,0,0,0,0,0,0
	};

	void Clean()
	{
		ImageContours.clear();
		ImageQuads.clear();
		ImageMarkers.clear();
	}
	void ConvertColor()
	{
		cvtColor(Image, ImageGray, CV_BGR2GRAY);
		adaptiveThreshold(ImageGray, ImageAdaptiveBinary, 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY_INV, 7, 7);
	}
	void GetContours(int ContourCountThreshold)
	{
		vector<vector<Point>> AllContours;
		findContours(ImageAdaptiveBinary, AllContours, CV_RETR_LIST, CV_CHAIN_APPROX_NONE);
		for (size_t i = 0;i < AllContours.size();++i)
		{
			int contourSize = AllContours[i].size();
			if (contourSize > ContourCountThreshold)
			{
				ImageContours.push_back(AllContours[i]);
			}
		}
	}
	void FindQuads(int ContourLengthThreshold)
	{
		vector<vector<Point2f>> PossibleQuads;
		for (int i = 0;i < ImageContours.size();++i)    //��Ⲣֻ������4������Ķ���Σ�����ʱ�봢�涥��
		{
			vector<Point2f> InDetectPoly;
			approxPolyDP(ImageContours[i], InDetectPoly, ImageContours[i].size() * 0.05, true);
			if (InDetectPoly.size() != 4) continue;
			if (!isContourConvex(InDetectPoly))	continue;
			float MinDistance = 1e10;
			for (int j = 0;j < 4;++j)
			{
				Point2f Side = InDetectPoly[j] - InDetectPoly[(j + 1) % 4];
				float SquaredSideLength = Side.dot(Side);
				MinDistance = min(MinDistance, SquaredSideLength);
			}
			if (MinDistance < ContourLengthThreshold) continue;
			vector<Point2f> TargetPoints;
			for (int j = 0;j < 4;++j)  //�����ĸ���
			{
				TargetPoints.push_back(Point2f(InDetectPoly[j].x, InDetectPoly[j].y));
			}
			Point2f Vector1 = TargetPoints[1] - TargetPoints[0];    //��֤��ʱ�봢�涥��
			Point2f Vector2 = TargetPoints[2] - TargetPoints[0];
			if (Vector2.cross(Vector1) < 0.0) swap(TargetPoints[1], TargetPoints[3]);
			PossibleQuads.push_back(TargetPoints);
		}
		vector<pair<int, int>> TooNearQuads;             //ɾ�����鿿��̫���Ķ����
		for (int i = 0;i < PossibleQuads.size();++i)
		{
			vector<Point2f>& Quad1 = PossibleQuads[i]; //��������maker�ı���֮��ľ��룬�����֮�����͵�ƽ��ֵ����ƽ��ֵ��С������Ϊ����maker�����,����һ���ı��η����Ƴ����С�		                  
			for (int j = i + 1;j < PossibleQuads.size();++j)
			{
				vector<Point2f>& Quad2 = PossibleQuads[j];
				float distSquared = 0;
				float x1Sum = 0.0, x2Sum = 0.0, y1Sum = 0.0, y2Sum = 0.0, dx = 0.0, dy = 0.0;
				for (int c = 0;c < 4;++c)
				{
					x1Sum += Quad1[c].x;
					x2Sum += Quad2[c].x;
					y1Sum += Quad1[c].y;
					y2Sum += Quad2[c].y;
				}
				x1Sum /= 4;	x2Sum /= 4;	y1Sum /= 4;	y2Sum /= 4;
				dx = x1Sum - x2Sum;
				dy = y1Sum - y2Sum;
				distSquared = sqrt(dx*dx + dy*dy);
				if (distSquared < 50)
				{
					TooNearQuads.push_back(pair<int, int>(i, j));
				}
			}//�Ƴ������ڵ�Ԫ�ضԵı�ʶ
		}//����������������marker�ڲ����ĸ���ľ���ͣ�������ͽ�С�ģ���removlaMask������ǣ�������Ϊ���յ�detectedMarkers 
		vector<bool> RemovalMask(PossibleQuads.size(), false);//����Vector���󣬲�������������һ���������������ڶ�����Ԫ�ء�
		for (int i = 0;i < TooNearQuads.size();++i)
		{
			float p1 = CalculatePerimeter(PossibleQuads[TooNearQuads[i].first]);  //����һ�������ı��ε��ܳ�
			float p2 = CalculatePerimeter(PossibleQuads[TooNearQuads[i].second]);
			int removalIndex;  //˭�ܳ�С���Ƴ�˭
			if (p1 > p2) removalIndex = TooNearQuads[i].second;
			else removalIndex = TooNearQuads[i].first;
			RemovalMask[removalIndex] = true;
		}
		//���غ�ѡ���Ƴ������ı������ܳ���С���Ǹ�������������ı��εĶ����С�//���ؿ��ܵĶ���
		for (size_t i = 0;i < PossibleQuads.size();++i)
		{
			if (!RemovalMask[i]) ImageQuads.push_back(PossibleQuads[i]);
		}
	}
	void TransformVerifyQuads()
	{
		//Ϊ�˵õ���Щ���εı��ͼ�����ǲ��ò�ʹ��͸�ӱ任ȥ�ָ�(unwarp)�����ͼ���������Ӧ��ʹ��cv::getPerspectiveTransform������
		//�����ȸ����ĸ���Ӧ�ĵ��ҵ�͸�ӱ任����һ�������Ǳ�ǵ����꣬�ڶ����������α��ͼ������ꡣ����ı任����ѱ��ת���ɷ��Σ��Ӷ��������Ƿ�����
		Mat FlatQuad;
		for (size_t i = 0;i < ImageQuads.size();++i)
		{
			vector<Point2f>& Quad = ImageQuads[i];	//�ҵ�͸��ת�����󣬻�þ��������������ͼ// �ҵ�͸��ͶӰ�����ѱ��ת���ɾ��Σ�����ͼ���ı��ζ������꣬���ͼ�����Ӧ���ı��ζ������� 
			Mat TransformMartix = getPerspectiveTransform(Quad, FlatMarkerCorners);//����ԭʼͼ��ͱ任֮���ͼ��Ķ�Ӧ4���㣬����Եõ��任���� (����任����Ľǣ���׼�ķ����Ľǣ�
			warpPerspective(ImageGray, FlatQuad, TransformMartix, FlatMarkerSize);
			threshold(FlatQuad, FlatQuad, 0, 255, THRESH_OTSU);
			if (MatchQuadWithMarker(FlatQuad))
			{
				ImageMarkers.push_back(ImageQuads[i]);
			}
			else
			{
				for (int j = 0;j < 3;++j)
				{
					rotate(FlatQuad, FlatQuad, ROTATE_90_CLOCKWISE);
					if (MatchQuadWithMarker(FlatQuad))
					{
						ImageMarkers.push_back(ImageQuads[i]);
						break;
					}
				}
			}
		}
	}
	void DrawMarkerBorder(Scalar Color)
	{
		for (vector<Point2f> Marker : ImageMarkers)
		{
			line(Image, Marker[0], Marker[1], Color, 2, CV_AA);
			line(Image, Marker[1], Marker[2], Color, 2, CV_AA);
			line(Image, Marker[2], Marker[3], Color, 2, CV_AA);
			line(Image, Marker[3], Marker[0], Color, 2, CV_AA);//CV_AA�ǿ����*/
		}
	}
	void DrawImageAboveMarker()
	{
		if (ImageToDraw.empty())return;
		vector<Point2f> ImageCorners = { Point2f(0,0),Point2f(ImageToDraw.cols - 1,0),Point2f(ImageToDraw.cols - 1,ImageToDraw.rows - 1),Point2f(0,ImageToDraw.rows - 1) };
		Mat_<Vec3b> ImageWarp = Image;
		for (vector<Point2f> Marker : ImageMarkers)
		{
			Mat TransformMartix = getPerspectiveTransform(ImageCorners, Marker);//����ԭʼͼ��ͱ任֮���ͼ��Ķ�Ӧ4���㣬����Եõ��任���� (����任����Ľǣ���׼�ķ����Ľǣ�
			Mat_<Vec3b> Result(Size(Image.cols, Image.rows), CV_8UC3);
			warpPerspective(ImageToDraw, Result, TransformMartix, Size(Image.cols, Image.rows));
			for (int r = 0;r < Image.rows;++r)
			{
				for (int c = 0;c < Image.cols;++c)
				{
					if (Result(r, c) != Vec3b(0, 0, 0))
					{
						ImageWarp(r, c) = Result(r, c);
					}
				}
			}
		}
	}

	bool MatchQuadWithMarker(Mat & Quad)
	{
		int  Pos = 0;
		for (int r = 2;r < 33;r += 5)
		{
			for (int c = 2;c < 33;c += 5)
			{
				uchar V = Quad.at<uchar>(r, c);
				uchar K = CorrectMarker[Pos];
				if (K != V)
					return false;
				Pos++;
			}
		}
		return true;
	}
	float CalculatePerimeter(const vector<Point2f> &Points)  //�������ܳ���
	{
		float sum = 0, dx, dy;
		for (size_t i = 0;i < Points.size();++i)
		{
			size_t i2 = (i + 1) % Points.size();
			dx = Points[i].x - Points[i2].x;
			dy = Points[i].y - Points[i2].y;
			sum += sqrt(dx*dx + dy*dy);
		}
		return sum;
	}
public:
	Mat ImageToDraw;
	MarkerBasedARProcessor()
	{
		FlatMarkerSize = Size(35, 35);
		FlatMarkerCorners = { Point2f(0,0),Point2f(FlatMarkerSize.width - 1,0),Point2f(FlatMarkerSize.width - 1,FlatMarkerSize.height - 1),Point2f(0,FlatMarkerSize.height - 1) };
	}
	Mat Process(Mat& Image)
	{
		Clean();
		Image.copyTo(this->Image);
		ConvertColor();
		GetContours(50);
		FindQuads(100);
		TransformVerifyQuads();
		DrawMarkerBorder(Scalar(255, 255, 255));
		DrawImageAboveMarker();
		return this->Image;
	}
};

int main()
{
	Mat Frame, ProceedFrame;
	VideoCapture Camera(0);
	while (!Camera.isOpened());
	MarkerBasedARProcessor Processor;
	Processor.ImageToDraw = imread("ImageToDraw.jpg");
	while (waitKey(1))
	{
		Camera >> Frame;
		imshow("Frame", Frame);
		ProceedFrame = Processor.Process(Frame);
		imshow("ProceedFrame", ProceedFrame);
	}
}