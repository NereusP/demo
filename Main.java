package test;

public class Main {
	public static void main(String[] args) {
		Solution solution = new Solution();
		int[] nums1 = {1,3,7,8};
		int[] nums2 = {3,4};
		System.out.println("mid "+solution.method(nums1, nums2));
	}
}

class Solution {
	public double method(int[] nums1, int[] nums2) {
		int length1 = nums1.length;
		int length2 = nums2.length;
		if (length1 == 0) {
			if (length2 %2 == 0) {
				return (nums2[length2/2-1] + nums2[length2/2])/2.0;
			}
			else {
				return nums2[length2/2];
			}
		}
		else if (length2 == 0) {
			if (length1 %2 == 0) {
				return (nums1[length1/2-1] + nums1[length1/2])/2.0;
			}
			else {
				return nums1[length1/2];
			}
		}
		else {
			return binarySerchMid(nums1, nums2);
		}
	}

	private double binarySerchMid(int[] nums1,int[] nums2) {
		int length1 = nums1.length;
		int length2 = nums2.length;
		int midLength = (length1+length2+1)/2;
		if (length1 > length2) {
			int[] temp = nums1;
			nums1 = nums2;
			nums2 = temp;
			int tempLeng = length1;
			length1 = length2;
			length2 = tempLeng;
		}

		int midA = 0;
		int midB = 0;
		int left = 0, right = length1;
		int maxLeft = 0, minRight = 0;
		while (left <= right) {
			midA = (left + right)/2;
			midB = midLength - midA;
			if ((midA < length1) && (nums1[midA] < nums2[midB-1])) {
				left = midA+1;
			}
			else if ((midA > 0) && (nums1[midA-1] > nums2[midB])) {
				right = midA-1;
			}
			else {
				break;
			}
		}
		System.out.println("midA = "+midA+",midB = "+midB);
		if (midA == 0) {
			maxLeft = nums2[midB-1];
		}
		else if (midB == 0) {
			maxLeft = nums1[midA-1];
		}
		else {
			maxLeft = Math.max(nums1[midA-1], nums2[midB-1]);
		}
		if ((length1+length2)%2 != 0) {
			return maxLeft;
		}
		else {
			if (midA == length1) {
				minRight = nums2[midB];
			}
			else if (midB == length2) {
				minRight = nums1[midA];
			}
			else {
				minRight = Math.min(nums1[midA], nums2[midB]);
			}

			return (maxLeft+minRight)/2.0;
		}
	}
}
