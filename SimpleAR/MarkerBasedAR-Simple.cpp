#include<opencv2\opencv.hpp>
#include<iostream>
#include<math.h>

using namespace cv;
using namespace std;

class MarkerBasedARProcessor
{
	Mat Image, ImageGray, ImageAdaptiveBinary; //�ֱ��� ԭͼ�� �Ҷ�ͼ�� ����Ӧ��ֵ��ͼ��
	vector<vector<Point>> ImageContours; //ͼ�����б߽���Ϣ
	vector<vector<Point2f>> ImageQuads, ImageMarkers; //ͼ�������ı��� �� ��֤�ɹ����ı���

	vector<Point2f> FlatMarkerCorners; //�����λ����ʱ�õ�����Ϣ
	Size FlatMarkerSize; //�����λ����ʱ�õ�����Ϣ

	//7x7�ڰױ�ǵ���ɫ��Ϣ
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

	void Clean() // ������һ֡����ǰ�ĳ�ʼ��
	{
		ImageContours.clear();
		ImageQuads.clear();
		ImageMarkers.clear();
	}
	void ConvertColor() //ת��ͼƬ��ɫ
	{
		cvtColor(Image, ImageGray, CV_BGR2GRAY);
		adaptiveThreshold(ImageGray, ImageAdaptiveBinary, 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY_INV, 7, 7);
	}
	void GetContours(int ContourCountThreshold) //��ȡͼƬ���б߽�
	{
		vector<vector<Point>> AllContours; // ���б߽���Ϣ
		findContours(ImageAdaptiveBinary, AllContours,
			CV_RETR_LIST, CV_CHAIN_APPROX_NONE); // ������Ӧ��ֵ��ͼ��Ѱ�ұ߽�
		for (size_t i = 0;i < AllContours.size();++i) // ֻ���������ֵ�ı߽�
		{
			int contourSize = AllContours[i].size();
			if (contourSize > ContourCountThreshold)
			{
				ImageContours.push_back(AllContours[i]);
			}
		}
	}
	void FindQuads(int ContourLengthThreshold) //Ѱ�������ı���
	{
		vector<vector<Point2f>> PossibleQuads;
		for (int i = 0;i < ImageContours.size();++i)
		{
			vector<Point2f> InDetectPoly;
			approxPolyDP(ImageContours[i], InDetectPoly,
				ImageContours[i].size() * 0.05, true); // �Ա߽���ж�������
			if (InDetectPoly.size() != 4) continue;// ֻ���ı��θ���Ȥ
			if (!isContourConvex(InDetectPoly)) continue; // ֻ��͹�ı��θ���Ȥ
			float MinDistance = 1e10; // Ѱ����̱�
			for (int j = 0;j < 4;++j)
			{
				Point2f Side = InDetectPoly[j] - InDetectPoly[(j + 1) % 4];
				float SquaredSideLength = Side.dot(Side);
				MinDistance = min(MinDistance, SquaredSideLength);
			}
			if (MinDistance < ContourLengthThreshold) continue; // ��̱߱��������ֵ
			vector<Point2f> TargetPoints;
			for (int j = 0;j < 4;++j) // �����ĸ���
			{
				TargetPoints.push_back(Point2f(InDetectPoly[j].x, InDetectPoly[j].y));
			}
			Point2f Vector1 = TargetPoints[1] - TargetPoints[0]; // ��ȡһ���ߵ�����
			Point2f Vector2 = TargetPoints[2] - TargetPoints[0]; // ��ȡһ��б�ߵ�����
			if (Vector2.cross(Vector1) < 0.0) // �����������Ĳ�� �жϵ��Ƿ�Ϊ��ʱ�봢��
				swap(TargetPoints[1], TargetPoints[3]); // �������0��Ϊ˳ʱ�룬��Ҫ����
			PossibleQuads.push_back(TargetPoints); // ��������ܵ��ı��Σ����н�һ���ж�
		}
		vector<pair<int, int>> TooNearQuads; // ׼��ɾ�����鿿̫���Ķ����
		for (int i = 0;i < PossibleQuads.size();++i)
		{
			vector<Point2f>& Quad1 = PossibleQuads[i]; // ��һ��             
			for (int j = i + 1;j < PossibleQuads.size();++j)
			{
				vector<Point2f>& Quad2 = PossibleQuads[j]; // �ڶ���
				float distSquared = 0;
				float x1Sum = 0.0, x2Sum = 0.0, y1Sum = 0.0, y2Sum = 0.0, dx = 0.0, dy = 0.0;
				for (int c = 0;c < 4;++c)
				{
					x1Sum += Quad1[c].x;
					x2Sum += Quad2[c].x;
					y1Sum += Quad1[c].y;
					y2Sum += Quad2[c].y;
				}
				x1Sum /= 4; x2Sum /= 4; y1Sum /= 4; y2Sum /= 4; // ����ƽ��ֵ���е㣩
				dx = x1Sum - x2Sum;
				dy = y1Sum - y2Sum;
				distSquared = sqrt(dx*dx + dy*dy); // ����������ξ���
				if (distSquared < 50)
				{
					TooNearQuads.push_back(pair<int, int>(i, j)); // ������׼���޳�
				}
			}
		}
		vector<bool> RemovalMask(PossibleQuads.size(), false); // �Ƴ�����б�
		for (int i = 0;i < TooNearQuads.size();++i)
		{
			float p1 = CalculatePerimeter(PossibleQuads[TooNearQuads[i].first]);  //���ܳ�
			float p2 = CalculatePerimeter(PossibleQuads[TooNearQuads[i].second]);
			int removalIndex;  //�Ƴ��ܳ�С�Ķ����
			if (p1 > p2) removalIndex = TooNearQuads[i].second;
			else removalIndex = TooNearQuads[i].first;
			RemovalMask[removalIndex] = true;
		}
		for (size_t i = 0;i < PossibleQuads.size();++i)
		{
			// ֻ¼��û���޳��Ķ����
			if (!RemovalMask[i]) ImageQuads.push_back(PossibleQuads[i]);
		}
	}
	void TransformVerifyQuads() //�任Ϊ�����β���֤�Ƿ�Ϊ���
	{
		Mat FlatQuad;
		for (size_t i = 0;i < ImageQuads.size();++i)
		{
			vector<Point2f>& Quad = ImageQuads[i];
			Mat TransformMartix = getPerspectiveTransform(Quad, FlatMarkerCorners);
			warpPerspective(ImageGray, FlatQuad, TransformMartix, FlatMarkerSize);
			threshold(FlatQuad, FlatQuad, 0, 255, THRESH_OTSU); // ��Ϊ��ֵ��ͼ��
			if (MatchQuadWithMarker(FlatQuad)) // ����ȷ��Ǳȶ�
			{
				ImageMarkers.push_back(ImageQuads[i]); // �ɹ����¼
			}
			else // ���ʧ�ܣ�����ת��ÿ��90�Ƚ��бȶ�
			{
				for (int j = 0;j < 3;++j)
				{
					rotate(FlatQuad, FlatQuad, ROTATE_90_CLOCKWISE);
					if (MatchQuadWithMarker(FlatQuad))
					{
						ImageMarkers.push_back(ImageQuads[i]); // �ɹ����¼
						break;
					}
				}
			}
		}
	} //�任Ϊ�����β���֤�Ƿ�Ϊ���

	void DrawMarkerBorder(Scalar Color) //���Ʊ�Ǳ߽�
	{
		for (vector<Point2f> Marker : ImageMarkers)
		{
			line(Image, Marker[0], Marker[1], Color, 2, CV_AA);
			line(Image, Marker[1], Marker[2], Color, 2, CV_AA);
			line(Image, Marker[2], Marker[3], Color, 2, CV_AA);
			line(Image, Marker[3], Marker[0], Color, 2, CV_AA);//CV_AA�ǿ����
		}
	}
	void DrawImageAboveMarker() //�ڱ���ϻ�ͼ
	{
		if (ImageToDraw.empty())return;
		vector<Point2f> ImageCorners = { Point2f(0,0),Point2f(ImageToDraw.cols - 1,0),Point2f(ImageToDraw.cols - 1,ImageToDraw.rows - 1),Point2f(0,ImageToDraw.rows - 1) };
		Mat_<Vec3b> ImageWarp = Image;
		for (vector<Point2f> Marker : ImageMarkers)
		{
			Mat TransformMartix = getPerspectiveTransform(ImageCorners, Marker);
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

	bool MatchQuadWithMarker(Mat & Quad) // �����������Ƿ�Ϊ���
	{
		int  Pos = 0;
		for (int r = 2;r < 33;r += 5) // ������ͼ���СΪ(35,35)
		{
			for (int c = 2;c < 33;c += 5)// ��ȡÿ��ͼ�����ĵ�
			{
				uchar V = Quad.at<uchar>(r, c);
				uchar K = CorrectMarker[Pos];
				if (K != V) // ����ȷ�����ɫ��Ϣ�ȶ�
					return false;
				Pos++;
			}
		}
		return true;
	}
	float CalculatePerimeter(const vector<Point2f> &Points)  // �����ܳ�
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
	Mat ImageToDraw; // Ҫ�ڱ���ϻ��Ƶ�ͼ��
	MarkerBasedARProcessor() // ���캯��
	{
		FlatMarkerSize = Size(35, 35);
		FlatMarkerCorners = { Point2f(0,0),Point2f(FlatMarkerSize.width - 1,0),Point2f(FlatMarkerSize.width - 1,FlatMarkerSize.height - 1),Point2f(0,FlatMarkerSize.height - 1) };
	}
	Mat Process(Mat& Image)// ����һ֡ͼ��
	{
		Clean(); // ��һ֡��ʼ��
		Image.copyTo(this->Image); // ����ԭʼͼ��Image��
		ConvertColor(); // ת����ɫ
		GetContours(50); // ��ȡ�߽�
		FindQuads(100); // Ѱ���ı���
		TransformVerifyQuads(); // ���β�У���ı���
		DrawMarkerBorder(Scalar(255, 255, 255)); // �ڵõ��ı����Χ���߽�
		DrawImageAboveMarker(); // �ڱ���ϻ�ͼ
		return this->Image; // ���ؽ��ͼ��
	}
};

int main()
{
	Mat Frame, ProceedFrame;
	VideoCapture Camera(0); // ��ʼ�����
	while (!Camera.isOpened()); // �ȴ�����������
	MarkerBasedARProcessor Processor; // ����һ��AR������
	Processor.ImageToDraw = imread("ImageToDraw.jpg"); // �������ͼ��
	while (waitKey(1)) // ÿ��ѭ���ӳ�1ms
	{
		Camera >> Frame; // ��һ֡
		imshow("Frame", Frame); // ��ʾԭʼͼ��
		ProceedFrame = Processor.Process(Frame); // ����ͼ��
		imshow("ProceedFrame", ProceedFrame); // ��ʾ���ͼ��
	}
}