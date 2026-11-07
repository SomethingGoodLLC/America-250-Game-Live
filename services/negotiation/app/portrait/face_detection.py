"""
Face Detection and Preprocessing for Portrait Animation

Detects faces in uploaded images, crops to face region, and normalizes
for optimal animation quality.
"""

import logging

import numpy as np
from PIL import Image

logger = logging.getLogger(__name__)


class FaceDetector:
    """
    Face detection and preprocessing for portrait images.

    Uses dlib or opencv for face detection with fallback to center crop.
    """

    def __init__(self, target_size: int = 512):
        """
        Initialize face detector.

        Args:
            target_size: Target size for output images (square)
        """
        self.target_size = target_size
        self.detector = None
        self.predictor = None

        # Try to load dlib face detector
        try:
            import dlib
            self.detector = dlib.get_frontal_face_detector()
            logger.info("dlib face detector loaded")
        except ImportError:
            logger.warning("dlib not available, will try opencv")

            # Try opencv as fallback
            try:
                import cv2
                cascade_path = cv2.data.haarcascades + 'haarcascade_frontalface_default.xml'
                self.detector = cv2.CascadeClassifier(cascade_path)
                logger.info("OpenCV face detector loaded")
            except ImportError:
                logger.warning("No face detector available - will use center crop fallback")

    def detect_and_crop(
        self,
        image: np.ndarray,
        padding: float = 0.3
    ) -> tuple[np.ndarray, dict]:
        """
        Detect face and crop to face region.

        Args:
            image: Input image (H, W, 3) RGB
            padding: Padding around face as fraction of face size

        Returns:
            Tuple of (cropped_image, metadata)
            - cropped_image: Cropped and resized image
            - metadata: Dict with face bbox and detection info
        """
        h, w = image.shape[:2]

        # Try face detection
        bbox = self._detect_face(image)

        if bbox is None:
            # Fallback: use center crop with aspect ratio preservation
            logger.warning("No face detected, using center crop fallback")
            bbox = self._fallback_bbox(w, h)
            metadata = {"face_detected": False, "method": "center_crop"}
        else:
            metadata = {"face_detected": True, "method": "detector", "bbox": bbox}

        # Expand bbox with padding
        x1, y1, x2, y2 = bbox
        face_w = x2 - x1
        face_h = y2 - y1

        pad_w = int(face_w * padding)
        pad_h = int(face_h * padding)

        x1 = max(0, x1 - pad_w)
        y1 = max(0, y1 - pad_h)
        x2 = min(w, x2 + pad_w)
        y2 = min(h, y2 + pad_h)

        # Make square by expanding shorter dimension
        crop_w = x2 - x1
        crop_h = y2 - y1

        if crop_w > crop_h:
            # Expand height
            diff = crop_w - crop_h
            y1 = max(0, y1 - diff // 2)
            y2 = min(h, y2 + diff // 2)
        elif crop_h > crop_w:
            # Expand width
            diff = crop_h - crop_w
            x1 = max(0, x1 - diff // 2)
            x2 = min(w, x2 + diff // 2)

        # Crop image
        cropped = image[y1:y2, x1:x2]

        # Resize to target size
        cropped_pil = Image.fromarray(cropped)
        cropped_pil = cropped_pil.resize(
            (self.target_size, self.target_size),
            Image.Resampling.LANCZOS
        )
        cropped_resized = np.array(cropped_pil)

        metadata.update({
            "crop_bbox": (x1, y1, x2, y2),
            "original_size": (w, h),
            "target_size": self.target_size
        })

        logger.info(
            f"Face cropped and resized: {w}x{h} -> {self.target_size}x{self.target_size}",
            extra=metadata
        )

        return cropped_resized, metadata

    def _detect_face(self, image: np.ndarray) -> tuple[int, int, int, int] | None:
        """
        Detect face in image using available detector.

        Returns:
            Bounding box (x1, y1, x2, y2) or None if no face detected
        """
        if self.detector is None:
            return None

        # Try dlib detector
        if callable(self.detector):
            try:
                # Convert to grayscale for dlib
                if len(image.shape) == 3:
                    gray = np.mean(image, axis=2).astype(np.uint8)
                else:
                    gray = image

                faces = self.detector(gray, 1)

                if len(faces) == 0:
                    return None

                # Use largest face
                face = max(faces, key=lambda f: f.width() * f.height())
                return (face.left(), face.top(), face.right(), face.bottom())

            except Exception as e:
                logger.error(f"dlib face detection failed: {e}")
                return None

        # Try opencv detector
        try:
            import cv2
            gray = cv2.cvtColor(image, cv2.COLOR_RGB2GRAY)
            faces = self.detector.detectMultiScale(
                gray,
                scaleFactor=1.1,
                minNeighbors=5,
                minSize=(30, 30)
            )

            if len(faces) == 0:
                return None

            # Use largest face
            x, y, w, h = max(faces, key=lambda f: f[2] * f[3])
            return (x, y, x + w, y + h)

        except Exception as e:
            logger.error(f"OpenCV face detection failed: {e}")
            return None

    def _fallback_bbox(self, width: int, height: int) -> tuple[int, int, int, int]:
        """
        Generate fallback bounding box (center crop).

        Assumes face is in center ~60% of image.
        """
        # Use center 60% of image
        size = min(width, height) * 0.6

        center_x = width // 2
        center_y = height // 2

        half_size = int(size // 2)

        x1 = max(0, center_x - half_size)
        y1 = max(0, center_y - half_size)
        x2 = min(width, center_x + half_size)
        y2 = min(height, center_y + half_size)

        return (x1, y1, x2, y2)

    def align_face(
        self,
        image: np.ndarray
    ) -> tuple[np.ndarray, dict]:
        """
        Align face using facial landmarks (if predictor available).

        This improves animation quality by normalizing head pose.

        Args:
            image: Input image with detected face

        Returns:
            Tuple of (aligned_image, metadata)
        """
        # TODO: Implement facial landmark detection and alignment
        # Requires dlib shape predictor model
        # For now, just return original image

        metadata = {"aligned": False, "reason": "predictor_not_loaded"}
        return image, metadata

    def enhance_image(
        self,
        image: np.ndarray,
        contrast: float = 1.1,
        brightness: int = 0
    ) -> np.ndarray:
        """
        Apply basic image enhancements for better animation results.

        Args:
            image: Input image
            contrast: Contrast multiplier
            brightness: Brightness adjustment (-50 to 50)

        Returns:
            Enhanced image
        """
        # Convert to float for processing
        img_float = image.astype(np.float32)

        # Apply contrast
        img_float = (img_float - 127.5) * contrast + 127.5

        # Apply brightness
        img_float = img_float + brightness

        # Clip and convert back
        img_enhanced = np.clip(img_float, 0, 255).astype(np.uint8)

        return img_enhanced


def preprocess_portrait(
    image_bytes: bytes,
    target_size: int = 512,
    padding: float = 0.3,
    enhance: bool = True
) -> tuple[np.ndarray, dict]:
    """
    Complete preprocessing pipeline for portrait images.

    Args:
        image_bytes: Image file bytes (JPEG/PNG)
        target_size: Target output size (square)
        padding: Face crop padding
        enhance: Apply image enhancements

    Returns:
        Tuple of (processed_image, metadata)
    """
    # Load image
    import io

    from PIL import Image

    img_pil = Image.open(io.BytesIO(image_bytes)).convert("RGB")
    img_array = np.array(img_pil)

    # Initialize detector
    detector = FaceDetector(target_size=target_size)

    # Detect and crop face
    cropped, metadata = detector.detect_and_crop(img_array, padding=padding)

    # Enhance if requested
    if enhance:
        cropped = detector.enhance_image(cropped)
        metadata["enhanced"] = True

    return cropped, metadata

